/*jshint strict: false */
'use strict';

////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2014-2016 ArangoDB GmbH, Cologne, Germany
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
/// @author Jan Steemann
////////////////////////////////////////////////////////////////////////////////

const arangodb = require("@arangodb");
const actions = require("@arangodb/actions");
const users = require("@arangodb/users");

function get_api_user(req, res) {
  require("internal").print("############ ", require("internal").inspect(req));
  
  if (req.suffix.length === 0) {
    actions.resultOk(req, res, actions.HTTP_OK, {
      result: users.all()
    });
    return;
  }

  if (req.suffix.length !== 1) {
    actions.resultBad(req, res, arangodb.ERROR_HTTP_BAD_PARAMETER);
    return;
  }

  var user = decodeURIComponent(req.suffix[0]);

  try {
    actions.resultOk(req, res, actions.HTTP_OK, users.document(user));
  } catch (err) {
    if (err.errorNum === arangodb.errors.ERROR_USER_NOT_FOUND.code) {
      actions.resultNotFound(req, res, arangodb.errors.ERROR_USER_NOT_FOUND.code);
    } else {
      throw err;
    }
  }
}

function post_api_user(req, res) {
  var json = actions.getJsonBody(req, res, actions.HTTP_BAD);

  if (json === undefined) {
    return;
  }

  if (req.suffix.length === 1) {
    // validate if a combination or username / password is valid
    const user = decodeURIComponent(req.suffix[0]);
    const result = users.isValid(user, json.passwd);

    if (result) {
      actions.resultOk(req, res, actions.HTTP_OK, {
        result: true
      });
    } else {
      actions.resultNotFound(req, res, arangodb.errors.ERROR_USER_NOT_FOUND.code);
    }
    return;
  }

  if (req.suffix.length !== 0) {
    // unexpected URL
    actions.resultBad(req, res, arangodb.ERROR_HTTP_BAD_PARAMETER);
    return;
  }

  const user = json.user;
  const doc = users.save(user, json.passwd, json.active, json.extra, json.changePassword);

  if (json.passwordToken) {
    users.setPasswordToken(user, json.passwordToken);
  }

  users.reload();

  actions.resultOk(req, res, actions.HTTP_CREATED, doc);
}

function put_api_user(req, res) {
  if (req.suffix.length !== 1) {
    actions.resultBad(req, res, arangodb.ERROR_HTTP_BAD_PARAMETER);
    return;
  }

  var user = decodeURIComponent(req.suffix[0]);

  var json = actions.getJsonBody(req, res, actions.HTTP_BAD);

  if (json === undefined) {
    return;
  }

  try {
    var doc = users.replace(user, json.passwd, json.active, json.extra, json.changePassword);
    users.reload();

    actions.resultOk(req, res, actions.HTTP_OK, doc);
  } catch (err) {
    if (err.errorNum === arangodb.errors.ERROR_USER_NOT_FOUND.code) {
      actions.resultNotFound(req, res, arangodb.errors.ERROR_USER_NOT_FOUND.code);
    } else {
      throw err;
    }
  }
}

function patch_api_user(req, res) {
  if (req.suffix.length !== 1) {
    actions.resultBad(req, res, arangodb.ERROR_HTTP_BAD_PARAMETER);
    return;
  }

  var user = decodeURIComponent(req.suffix[0]);
  var json = actions.getJsonBody(req, res, actions.HTTP_BAD);

  if (json === undefined) {
    return;
  }

  try {
    var doc = users.update(user, json.passwd, json.active, json.extra, json.changePassword);
    users.reload();

    actions.resultOk(req, res, actions.HTTP_OK, doc);
  } catch (err) {
    if (err.errorNum === arangodb.errors.ERROR_USER_NOT_FOUND.code) {
      actions.resultNotFound(req, res, arangodb.errors.ERROR_USER_NOT_FOUND.code);
    } else {
      throw err;
    }
  }
}

function delete_api_user(req, res) {
  if (req.suffix.length !== 1) {
    actions.resultBad(req, res, arangodb.ERROR_HTTP_BAD_PARAMETER);
    return;
  }

  var user = decodeURIComponent(req.suffix[0]);
  try {
    users.remove(user);
    users.reload();

    actions.resultOk(req, res, actions.HTTP_ACCEPTED, {});
  } catch (err) {
    if (err.errorNum === arangodb.errors.ERROR_USER_NOT_FOUND.code) {
      actions.resultNotFound(req, res, arangodb.errors.ERROR_USER_NOT_FOUND.code);
    } else {
      throw err;
    }
  }
}

actions.defineHttp({
  url: "_api/user",

  callback: function(req, res) {
    try {
      switch (req.requestType) {
        case actions.GET:
          get_api_user(req, res);
          break;

        case actions.POST:
          post_api_user(req, res);
          break;

        case actions.PUT:
          put_api_user(req, res);
          break;

        case actions.PATCH:
          patch_api_user(req, res);
          break;

        case actions.DELETE:
          delete_api_user(req, res);
          break;

        default:
          actions.resultUnsupported(req, res);
      }
    } catch (err) {
      actions.resultException(req, res, err, undefined, false);
    }
  }
});
