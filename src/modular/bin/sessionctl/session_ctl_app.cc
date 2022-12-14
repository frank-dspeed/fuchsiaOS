// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionctl/session_ctl_app.h"

#include <lib/syslog/cpp/macros.h>

#include <re2/re2.h>

#include "src/modular/bin/sessionctl/session_ctl_constants.h"

namespace modular {

SessionCtlApp::SessionCtlApp(fuchsia::modular::internal::BasemgrDebugPtr basemgr_debug,
                             fuchsia::modular::PuppetMasterPtr puppet_master,
                             fuchsia::sys::LoaderPtr sys_loader, const modular::Logger& logger,
                             async_dispatcher_t* const dispatcher)
    : basemgr_debug_(std::move(basemgr_debug)),
      puppet_master_(std::move(puppet_master)),
      sys_loader_(std::move(sys_loader)),
      logger_(logger),
      dispatcher_(dispatcher) {}

void SessionCtlApp::ExecuteCommand(std::string cmd, const fxl::CommandLine& command_line,
                                   CommandDoneCallback done) {
  if (cmd == kAddModCommandString) {
    ExecuteAddModCommand(command_line, std::move(done));
  } else if (cmd == kRestartSessionCommandString) {
    ExecuteRestartSessionCommand(std::move(done));
  } else {
    done(fpromise::error(""));
  }
}

void SessionCtlApp::ExecuteAddModCommand(const fxl::CommandLine& command_line,
                                         CommandDoneCallback done) {
  if (command_line.positional_args().size() == 1) {
    auto parsing_error = "Missing MOD_URL. Ex: sessionctl add_mod slider_mod";
    logger_.LogError(kAddModCommandString, parsing_error);
    done(fpromise::error(parsing_error));
    return;
  }

  // Get the mod url and default the mod name and story name to the mod url
  std::string mod_url = command_line.positional_args().at(1);

  if (mod_url.find("fuchsia-pkg://") != 0) {
    // |mod_url| is not a fuchsia-pkg URL. Continue without validating it.
    ExecuteAddModCommandInternal(mod_url, command_line, std::move(done));
    return;
  }

  ModPackageExists(
      mod_url, [this, mod_url, command_line, done = std::move(done)](bool exists) mutable {
        if (!exists) {
          done(fpromise::error(std::string("No package with URL " + mod_url + " was found")));
          return;
        }
        ExecuteAddModCommandInternal(mod_url, command_line, std::move(done));
      });
}

void SessionCtlApp::ExecuteAddModCommandInternal(std::string mod_url,
                                                 const fxl::CommandLine& command_line,
                                                 CommandDoneCallback done) {
  // If there's not a colon, resolve to the fuchsia package path
  if (mod_url.find(":") == std::string::npos) {
    mod_url = fxl::StringPrintf(kFuchsiaPkgPath, mod_url.c_str(), mod_url.c_str());
  }

  std::string mod_name = mod_url;
  std::string story_name = std::to_string(std::hash<std::string>{}(mod_url));

  if (command_line.HasOption(kStoryNameFlagString)) {
    command_line.GetOptionValue(kStoryNameFlagString, &story_name);
    // regex from src/sys/appmgr/realm.cc:168
    re2::RE2 story_name_regex("[0-9a-zA-Z\\.\\-_:#]+");
    if (!re2::RE2::PartialMatch(story_name, story_name_regex)) {
      auto parsing_error = "Bad characters in story_name: " + story_name;
      logger_.LogError(kStoryNameFlagString, parsing_error);
      done(fpromise::error(parsing_error));
      return;
    }
  } else {
    std::cout << "Using auto-generated --story_name value of " << story_name << std::endl;
  }

  command_line.GetOptionValue(kModNameFlagString, &mod_name);
  if (!command_line.HasOption(kModNameFlagString)) {
    std::cout << "Using auto-generated --mod_name value of " << mod_name << std::endl;
  }

  auto commands = MakeAddModCommands(mod_url, mod_name);

  std::map<std::string, std::string> params = {{kModUrlFlagString, mod_url},
                                               {kModNameFlagString, mod_name},
                                               {kStoryNameFlagString, story_name}};

  puppet_master_->ControlStory(story_name, story_puppet_master_.NewRequest());
  PostTaskExecuteStoryCommand(kAddModCommandString, std::move(commands), params, std::move(done));
}

void SessionCtlApp::ExecuteRestartSessionCommand(CommandDoneCallback done) {
  basemgr_debug_->RestartSession([this, done = std::move(done)]() {
    logger_.Log(kRestartSessionCommandString, std::vector<std::string>());
    done(fpromise::ok());
  });
}

std::vector<fuchsia::modular::StoryCommand> SessionCtlApp::MakeAddModCommands(
    const std::string& mod_url, const std::string& mod_name) {
  fuchsia::modular::Intent intent;
  intent.handler = mod_url;

  std::vector<fuchsia::modular::StoryCommand> commands;
  fuchsia::modular::StoryCommand command;

  // Add command to add or update the mod (it will be updated if the mod_name
  // already exists in the story).
  fuchsia::modular::AddMod add_mod;
  add_mod.mod_name_transitional = mod_name;
  intent.Clone(&add_mod.intent);
  // TODO(fxbug.dev/16775): Sessionctl takes in initial intent and other fields.

  command.set_add_mod(std::move(add_mod));
  commands.push_back(std::move(command));

  return commands;
}

void SessionCtlApp::PostTaskExecuteStoryCommand(
    const std::string command_name, std::vector<fuchsia::modular::StoryCommand> commands,
    std::map<std::string, std::string> params, CommandDoneCallback done) {
  async::PostTask(dispatcher_, [this, command_name, commands = std::move(commands), params,
                                done = std::move(done)]() mutable {
    ExecuteStoryCommand(std::move(commands), params.at(kStoryNameFlagString))
        ->Then([this, command_name, params, done = std::move(done)](bool has_error,
                                                                    std::string result) {
          if (has_error) {
            logger_.LogError(command_name, result);
            done(fpromise::error(result));
          } else {
            auto params_copy = params;
            params_copy.emplace(kStoryIdFlagString, result);
            logger_.Log(command_name, params_copy);
            done(fpromise::ok());
          }
        });
  });
}

modular::FuturePtr<bool, std::string> SessionCtlApp::ExecuteStoryCommand(
    std::vector<fuchsia::modular::StoryCommand> commands, const std::string& story_name) {
  story_puppet_master_->Enqueue(std::move(commands));

  auto fut = modular::Future<bool, std::string>::Create("Sessionctl StoryPuppetMaster::Execute");

  story_puppet_master_->Execute([fut](fuchsia::modular::ExecuteResult result) mutable {
    if (result.status == fuchsia::modular::ExecuteStatus::OK) {
      fut->Complete(false, result.story_id->c_str());
    } else {
      std::string error = fxl::StringPrintf("Puppet master returned status: %d and error: %s",
                                            (uint32_t)result.status, result.error_message->c_str());

      FX_LOGS(WARNING) << error << std::endl;
      fut->Complete(true, std::move(error));
    }
  });

  return fut;
}

void SessionCtlApp::ModPackageExists(std::string url, fit::function<void(bool)> done) {
  sys_loader_->LoadUrl(
      url, [done = std::move(done)](fuchsia::sys::PackagePtr package) { done(!!package); });
}

}  // namespace modular
