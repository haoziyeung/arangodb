////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2014-2016 ArangoDB GmbH, Cologne, Germany
/// Copyright 2004-2014 triAGENS GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Kaveh Vahedipour
/// @author Matthew Von-Maszewski
////////////////////////////////////////////////////////////////////////////////

#include "ActionBase.h"
#include "CreateDatabase.h"

#include "ApplicationFeatures/ApplicationServer.h"
#include "Basics/VelocyPackHelper.h"
#include "RestServer/DatabaseFeature.h"
#include "VocBase/Methods/Databases.h"

using namespace arangodb::application_features;
using namespace arangodb::maintenance;
using namespace arangodb::methods;

CreateDatabase::CreateDatabase(
  MaintenanceFeature& feature, ActionDescription const& desc)
  : ActionBase(feature, desc) {
  TRI_ASSERT(desc.has(DATABASE));
}

CreateDatabase::~CreateDatabase() {};

bool CreateDatabase::first() {

  VPackSlice users;
  auto database = _description.get(DATABASE);

  LOG_TOPIC(INFO, Logger::MAINTENANCE) << "creating database " << database;
  
  auto* systemVocbase =
    ApplicationServer::getFeature<DatabaseFeature>("Database")->systemDatabase();

  if (systemVocbase == nullptr) {
    LOG_TOPIC(FATAL, Logger::MAINTENANCE) << "could not determine _system database";
    FATAL_ERROR_EXIT();
  }
  
  // Assertion in constructor makes sure that we have DATABASE.
  _result = Databases::create(_description.get(DATABASE), users, properties());
  if (!_result.ok()) {
    LOG_TOPIC(ERR, Logger::MAINTENANCE)
      << "failed to create database " << database << ": " << _result;
  }

  LOG_TOPIC(INFO, Logger::MAINTENANCE) << "database  " << database << " created";
  return false;

}

arangodb::Result CreateDatabase::kill(Signal const& signal) {
  return actionError(
    TRI_ERROR_ACTION_OPERATION_UNABORTABLE, "Cannot kill CreateDatabase action");
}

arangodb::Result CreateDatabase::progress(double& progress) {
  progress = 0.5;
  return arangodb::Result(TRI_ERROR_NO_ERROR);
}

