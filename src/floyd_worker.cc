#include "floyd_mutex.h"
#include "floyd_worker.h"
#include "floyd.h"
#include "command.pb.h"
#include "logger.h"
namespace floyd {

FloydWorkerCliConn::FloydWorkerCliConn(const std::string& ip, const int port)
    : local_ip_(ip), local_port_(port + 100) {}

FloydWorkerCliConn::~FloydWorkerCliConn() {}

Status FloydWorkerCliConn::Connect() {
  pink::Status ret = PbCli::Connect(local_ip_, local_port_);
  Status s = static_cast<Status>(ret);
  return s;
}

Status FloydWorkerCliConn::GetResMessage(google::protobuf::Message* cmd_res) {
  pink::Status ret = Recv(cmd_res);
  Status s = static_cast<Status>(ret);
  if (!s.ok()) return s;
}

Status FloydWorkerCliConn::SendMessage(google::protobuf::Message* cmd) {
  pink::Status ret = Send(cmd);
  Status s = static_cast<Status>(ret);
  return s;
}

FloydWorkerConn::FloydWorkerConn(int fd, std::string& ip_port,
                                 pink::Thread* thread)
    : PbConn(fd, ip_port) {}

FloydWorkerConn::~FloydWorkerConn() {}

int FloydWorkerConn::DealMessage() {
  command_.ParseFromArray(rbuf_ + 4, header_len_);

  switch (command_.type()) {
    case command::Command::DirtyWrite: {
      set_is_reply(false);
      command::Command_Kv kv = command_.kv();
      std::string key = kv.key();
      std::string value = kv.value();
      Status ret = Floyd::db->Set(key, value);
      if (!ret.ok()) break;
    }
    case command::Command::Write: {
      set_is_reply(true);
      Status ret = Floyd::raft_con->HandleWriteCommand(command_);
      command_res_.set_type(command::CommandRes::Write);
      command::CommandRes_KvRet* kvr = new command::CommandRes_KvRet();
      if (!ret.ok())
        kvr->set_status(false);
      else
        kvr->set_status(true);
      // printf ("Write end key(%s) v(%s) ret(%s)\n", kvr->key
      command_res_.set_allocated_kvr(kvr);
      break;
    }
    case command::Command::Read: {
      set_is_reply(true);
      std::string value;
      LOG_DEBUG("MainThread: Read as Leader from Follower redirect: %s",
                ip_port().c_str());
      Status ret = Floyd::raft_con->HandleReadCommand(command_, value);
      command_res_.set_type(command::CommandRes::Read);
      command::CommandRes_KvRet* kvr = new command::CommandRes_KvRet();
      if (!ret.ok()) {
        kvr->set_status(false);
      } else {
        kvr->set_status(true);
        kvr->set_value(value);
      }
      command_res_.set_allocated_kvr(kvr);
      break;
    }
    case command::Command::ReadAll: {
      set_is_reply(true);
      KVMap kvMap;
      Status ret = Floyd::raft_con->HandleReadAllCommand(command_, kvMap);
      //@TODO construct return value,parse from map
      command_res_.set_type(command::CommandRes::ReadAll);
      command::CommandRes_KvAllRet* kvAllRet =
          new command::CommandRes_KvAllRet();
      if (!ret.ok()) {
        kvAllRet->set_status(false);
      } else {
        kvAllRet->set_status(true);
        KVMap::iterator it;
        for (it = kvMap.begin(); it != kvMap.end(); ++it) {
          command::CommandRes_Kv kv = command::CommandRes_Kv();
          kv.set_key(it->first);
          kv.set_value(it->first);
          *(kvAllRet->add_kvall()) = kv;
        }
      }
      command_res_.set_allocated_kvallr(kvAllRet);
      break;
    }

    case command::Command::TryLock: {
      // printf ("\nWorkerThread: deal TryLock cmd\n");
      LOG_DEBUG("MainThread: TryLock as Leader from Follower redirect: %s",
                ip_port().c_str());
      Status ret = Floyd::raft_con->HandleTryLockCommand(command_);

      set_is_reply(true);
      command_res_.set_type(command::CommandRes::TryLock);

      command::CommandRes_KvRet* kvr = new command::CommandRes_KvRet();
      if (!ret.ok() && !ret.IsTimeOut()) {
        // printf ("TryLock error as leader:%s\n", ret.ToString().c_str());
        kvr->set_status(false);
      } else {
        kvr->set_status(true);
        kvr->set_value(ret.ToString());
        // printf ("return TryLock result to follower result:%s\n",
        // ret.ToString().c_str());
      }

      command_res_.set_allocated_kvr(kvr);
      break;
    }
    case command::Command::UnLock: {
      // printf ("\nWorkerThread: deal UnLock cmd\n");

      Status ret = Floyd::raft_con->HandleUnLockCommand(command_);

      set_is_reply(true);
      command_res_.set_type(command::CommandRes::UnLock);

      command::CommandRes_KvRet* kvr = new command::CommandRes_KvRet();
      if (!ret.ok() && !ret.IsTimeOut()) {
        // printf ("UnLock error as leader:%s\n", ret.ToString().c_str());
        kvr->set_status(false);
      } else {
        kvr->set_status(true);
        kvr->set_value(ret.ToString());
        // printf ("return UnLock result to follower result:%s\n",
        // ret.ToString().c_str());
      }

      command_res_.set_allocated_kvr(kvr);
      break;
    }
    case command::Command::RaftVote: {
      set_is_reply(true);
      Floyd::raft_con->HandleRequestVote(command_, &command_res_);
      break;
    }
    case command::Command::RaftAppendEntries: {
      set_is_reply(true);
      Floyd::raft_con->HandleAppendEntries(command_, &command_res_);
      break;
    }
    case command::Command::SynRaftStage: {
      set_is_reply(true);
      command::CommandRes_RaftStageRes* rsr =
          new command::CommandRes_RaftStageRes();
      rsr->set_commitindex(Floyd::raft_con->GetCommitIndex());
      rsr->set_term(Floyd::raft_con->GetCurrentTerm());
      command_res_.set_type(command::CommandRes::SynRaftStage);
      command_res_.set_allocated_raftstage(rsr);
      break;
    }

    default:
      break;
  }

  res_ = &command_res_;

  return 0;
}

FloydWorkerThread::FloydWorkerThread(int port)
    : HolyThread<FloydWorkerConn>(port) {}

FloydWorkerThread::~FloydWorkerThread() {}
}
