/**
 *    Copyright (C) 2018-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#pragma once

#include "mongo/logv2/log_domain.h"
#include "mongo/logv2/log_domain_global.h"
#include "mongo/logv2/log_domain_internal.h"
#include "mongo/logv2/log_manager.h"
#include "mongo/unittest/unittest.h"

namespace mongo {
namespace logv2 {

class LogTestV2 : public unittest::Test {

public:
    LogTestV2() {
        LogDomainGlobal::ConfigurationOptions config;
        config.makeDisabled();

        ASSERT_OK(LogManager::global().getGlobalDomainInternal().configure(config));
    }

    ~LogTestV2() override {
        for (auto&& sink : _attachedSinks) {
            boost::log::core::get()->remove_sink(sink);
        }

        ASSERT_OK(LogManager::global().getGlobalDomainInternal().configure({}));
    }

    void attach(boost::shared_ptr<boost::log::sinks::sink> sink) {
        boost::log::core::get()->add_sink(std::move(sink));
        _attachedSinks.push_back(sink);
    }


private:
    std::vector<boost::shared_ptr<boost::log::sinks::sink>> _attachedSinks;
};

}  // namespace logv2
}  // namespace mongo