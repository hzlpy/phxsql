/*
	Tencent is pleased to support the open source community by making PhxSQL available.
	Copyright (C) 2016 THL A29 Limited, a Tencent company. All rights reserved.
	Licensed under the GNU General Public License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at
	
	https://opensource.org/licenses/GPL-2.0
	
	Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" basis, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.
*/

#include <string>
#include <inttypes.h>
#include <time.h>
#include "master_cache.h"

#include "phxbinlogsvr/client/phxbinlog_client_platform_info.h"
#include "phxcomm/phx_log.h"
#include "phxcomm/lock_manager.h"
#include "phxsqlproxyconfig.h"
#include "monitor_plugin.h"
#include "phxsqlproxyutil.h"

using namespace std;
using phxbinlogsvr::PhxBinlogClientPlatformInfo;
using phxbinlogsvr::PhxBinlogClient;
using phxsql::LogError;
using phxsql::LogInfo;

namespace phxsqlproxy {

MasterCache::MasterCache(PHXSqlProxyConfig * config) :
        config_(config) {
}

MasterCache::~MasterCache() {
}

bool MasterCache::IsMasterValid(const std::string & master_ip, uint64_t expired_time) {
    if (master_ip == "") {
        return false;
    }
    if (expired_time < (uint32_t)time(NULL)) {
        return false;
    }
    return true;
}

int MasterCache::GetMaster(std::string & master_ip, uint64_t & expired_time) {
    if (string(config_->GetSpecifiedMasterIP()) != "") {
        master_ip = config_->GetSpecifiedMasterIP();
        phxsql::LogVerbose("master is specified to [%s]", master_ip.c_str());
        return 0;
    }

    phxsql::RWLockManager lock(&mutex_, phxsql::RWLockManager::READ);

    if (IsMasterValid(GetGroupStatus().master_ip_, GetGroupStatus().expired_time_)) {
        master_ip = GetGroupStatus().master_ip_;
        expired_time = GetGroupStatus().expired_time_;
        return 0;
    }

    MonitorPluginEntry::GetDefault()->GetMonitorPlugin()->CheckMasterInvalid();
    phxsql::LogError("get master [%s] expiretime [%llu] current [%llu]", GetGroupStatus().master_ip_.c_str(),
                     GetGroupStatus().expired_time_, GetTimestampMS());
    return -__LINE__;
}

int MasterCache::UpdateGroupStatus(MasterStatus_t & group_status) {
    if (string(config_->GetSpecifiedMasterIP()) != "") {
        return 0;
    }

    string master_in_binlogsvr = "";
    uint32_t expired_time_in_binlogsvr = 0;
    std::shared_ptr<PhxBinlogClient> client = PhxBinlogClientPlatformInfo::GetDefault()->GetClientFactory()
            ->CreateClient();
    int ret = client->GetMaster(&master_in_binlogsvr, &expired_time_in_binlogsvr);
    if (ret != 0) {
        MonitorPluginEntry::GetDefault()->GetMonitorPlugin()->GetMasterInBinLogFail();
        phxsql::LogError("%s:%d GetMaster failed ret %d", __func__, __LINE__, ret);
        return -__LINE__;
    }

    if (IsMasterValid(master_in_binlogsvr, (uint64_t) expired_time_in_binlogsvr)) {
        phxsql::RWLockManager lock(&mutex_, phxsql::RWLockManager::WRITE);
        group_status.expired_time_ = expired_time_in_binlogsvr;
        group_status.master_ip_ = master_in_binlogsvr;
        return 0;
    }

    phxsql::LogError("%s:%d GetMaster success but (%s:%u) expired", __func__, __LINE__, master_in_binlogsvr.c_str(),
                     expired_time_in_binlogsvr);
    return -__LINE__;
}

}

