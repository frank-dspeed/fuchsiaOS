// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_FRAMEWORK_ENGINE_RUNNER_H_
#define SRC_SYS_FUZZING_FRAMEWORK_ENGINE_RUNNER_H_

#include <fuchsia/fuzzer/cpp/fidl.h>
#include <lib/fidl/cpp/interface_request.h>
#include <lib/zx/time.h>
#include <stddef.h>
#include <zircon/compiler.h>

#include <memory>
#include <vector>

#include "src/lib/fxl/macros.h"
#include "src/sys/fuzzing/common/async-deque.h"
#include "src/sys/fuzzing/common/async-types.h"
#include "src/sys/fuzzing/common/input.h"
#include "src/sys/fuzzing/common/runner.h"
#include "src/sys/fuzzing/framework/engine/adapter-client.h"
#include "src/sys/fuzzing/framework/engine/corpus.h"
#include "src/sys/fuzzing/framework/engine/coverage-client.h"
#include "src/sys/fuzzing/framework/engine/module-pool.h"
#include "src/sys/fuzzing/framework/engine/mutagen.h"
#include "src/sys/fuzzing/framework/engine/process-proxy.h"

namespace fuzzing {

// The concrete implementation of |Runner|.
class RunnerImpl final : public Runner {
 public:
  ~RunnerImpl() override = default;

  // Factory method.
  static RunnerPtr MakePtr(ExecutorPtr executor);

  void set_target_adapter_handler(TargetAdapterClient::RequestHandler handler) {
    target_adapter_.set_handler(std::move(handler));
  }
  void set_coverage_provider_handler(CoverageProviderClient::RequestHandler handler) {
    coverage_provider_.set_handler(std::move(handler));
  }

  // |Runner| method implementations.
  void AddDefaults(Options* options) override;
  __WARN_UNUSED_RESULT zx_status_t AddToCorpus(CorpusType corpus_type, Input input) override;
  Input ReadFromCorpus(CorpusType corpus_type, size_t offset) override;
  __WARN_UNUSED_RESULT zx_status_t ParseDictionary(const Input& input) override;
  Input GetDictionaryAsInput() const override;

  ZxPromise<> Configure(const OptionsPtr& options) override;
  ZxPromise<FuzzResult> Execute(Input input) override;
  ZxPromise<Input> Minimize(Input input) override;
  ZxPromise<Input> Cleanse(Input input) override;
  ZxPromise<Artifact> Fuzz() override;
  ZxPromise<> Merge() override;

  ZxPromise<> Stop() override;

  Status CollectStatus() override;

 protected:
  // |Reset|s input queues, records start times, and notifies monitors that the workflow is
  // starting. This method is called automatically by |Workflow::Start|.
  void StartWorkflow(Scope& scope) override;

  // Drops remaining inputs from queues, |Disconnect|s, and notifies monitors that the workflow is
  // done. This method is called automatically by |Workflow::Finish|.
  void FinishWorkflow() override;

 private:
  // Indicates how the engine should handle inputs that don't trigger an error.
  enum PostProcessing {
    // No-op.
    kNoPostProcessing,

    // Add the input's coverage to the overall coverage.
    kAccumulateCoverage,

    // Determine if any of the input's coverage is new. If so, record the coverage in the input and
    // add it to the live corpus.
    kMeasureCoverageAndKeepInputs,

    // Determine if any of the input's coverage is new. If so, add it to the overall coverage and
    // add the input to the live corpus.
    kAccumulateCoverageAndKeepInputs,
  };

  explicit RunnerImpl(ExecutorPtr executor);

  // Returns a promise to generate |num_inputs| inputs for testing by taking them from the
  // |processed| queue, mutating corpus elements, and sending them to the |generated| queue. If
  // |num_inputs| is 0, this will continuously generate new inputs until the |processed| queue is
  // closed and empty.
  // If a workflow does not depend on the coverage produced by each input, a |backlog| of inputs can
  // be generated, e.g. while waiting for target processes to respond. A |backlog| of 0 makes the
  // generation of each input depend on the processing of the previous one.
  ZxPromise<> GenerateInputs(size_t num_inputs, size_t backlog = 0);

  // Returns a promise to generate inputs from "cleaning" the provided |input|. To clean the input,
  // it replaces each byte with either a space or 0xFF. It keeps the change if the modified input
  // triggers an error, as determined by examining the test outputs that are sent to the |recycler|.
  Promise<> GenerateCleanInputs(const Input& input, std::shared_ptr<AsyncDeque<Artifact>> recycler);

  // Returns a promise to |GenerateInputs| and |TestInputs|. This will first iterate through the
  // seed and live corpora before mutating new inputs.  See |GenerateInputs| for details on
  // |backlog|.
  ZxPromise<Artifact> FuzzInputs(size_t backlog = 0);

  // Returns a promise to test a single |input|. The promise returns an artifact if found or an
  // error; in particular it returns |ZX_ERR_STOP| if it completed without finding an artifact.
  //
  // Inputs that do not trigger errors are analyzed according to the given |mode|.
  ZxPromise<Artifact> TestOneAsync(Input input, PostProcessing mode);

  // Returns a promise to test each non-empty input of a |corpus| in turn. The promise returns an
  // artifact if found or an error; in particular it returns |ZX_ERR_STOP| if it completed without
  // finding an artifact.
  //
  // Inputs that do not trigger errors are analyzed according to the given |mode|.
  //
  // Callers may specify they wish to |collect_errors| rather than stopping on the first artifact
  // found. See |TestInputs| for additional details.
  using InputsPtr = std::shared_ptr<std::vector<Input>>;
  ZxPromise<Artifact> TestCorpusAsync(CorpusPtr corpus, PostProcessing mode,
                                      InputsPtr collect_errors = nullptr);

  // Returns a promise that will read successive |Input|s from the |generated| queue, test them, and
  // release them to the |processed| queue.  The promise returns an artifact if found or an error;
  // in particular it returns |ZX_ERR_STOP| if it completed without finding an artifact to return.
  //
  // Inputs that do not trigger errors are analyzed according to the given |mode|.
  //
  // Callers may specify they wish to |collect_errors| rather than return artifacts. In this case,
  // errors will be cleared after the associated input has been saved, and testing will continue
  // until it exhausts inputs and returns |ZX_ERR_STOP|.
  ZxPromise<Artifact> TestInputs(PostProcessing mode, InputsPtr collect_errors = nullptr);

  // Returns a promise that checks a |previous| error returned from a |Test...| method. The promise
  // will complete successfully if the error was |ZX_ERR_STOP|, indicating the previous test did not
  // find an artifact. Otherwise, the error will be forwarded.
  ZxPromise<> CheckPrevious(zx_status_t previous);

  // Returns a promise that updates the |Monitor|s with status periodically. To end updates, exit
  // the given |workflow| scope.
  Promise<> MakePulsePromise(int64_t pulse_interval, Scope& workflow);

  // Returns a promise to coordinate the target processes to start a new run and get the next
  // |Input| to be tested from the |generated_| queue. The promise will return any unexpected errors
  // from target processes, or |ZX_ERR_STOP| if there are no more |Input|s. to be tested.
  ZxPromise<Input> Prepare(bool detect_leaks);

  // Sends the |input| to the |target_adapter_|, and returns a promise to wait for and return the
  // results of the run. On completion, the promise will either return the error encountered when
  // testing the |input| or whether a memory leak is suspected, i.e. the |input| caused more
  // |malloc|s than |free|s.
  Promise<bool, FuzzResult> RunOne(const Input& input);

  // Adds a new process or module for coverage collection as represented by the given |event|. New
  // processes are typically started as a result of the target adapter processing some input, and
  // there in turn add their modules.
  void AddCoverage(CoverageEvent event);

  // Returns a promise to determine the cause of an error in the target process identified by the
  // given |target_id|. In the case of multiple errors, only the first error is reported. However,
  // determining the error cause typically involves waiting for the process to terminate.
  // To accurately record which error is first, only the |target_id| is recorded initially, and the
  // other details are collected asynchronously using this method.
  Promise<bool, FuzzResult> GetFuzzResult(uint64_t target_id);

  // Examines the effects of a previous call to |RunOne| with the given |input|. This may collect
  // coverage and/or record new features for the input, according to the given |mode|.
  void Analyze(Input& input, PostProcessing mode);

  // Takes an |Input| and determines whether it should be retried to check for leaks or released for
  // reuse by a workflow to generate new |Input|s. Returns whether the next run will retry the input
  // to detect leaks, i.e. if the previous run was not |detecting| leaks, a leak was |suspected|,
  // and there are leak detection |attempts_left|.
  bool Recycle(Input&& input, size_t& attempts_left, bool suspected, bool detecting);

  // Disconnects from the target adapter and all target processes.
  void Disconnect();

  // Clears errors and any inputs in queues and returns the runner to a "clean slate". This is
  // useful when resuming after arrors, e.g. in a workflow that finds multiple errors.
  void Reset();

  // General configuration.
  OptionsPtr options_;
  uint32_t run_ = 0;

  // Time at which a workflow starts.
  zx::time start_ = zx::time::infinite_past();

  // Time at after which "pulse" status updates may be sent to monitors.
  zx::time pulse_start_;

  // Flag to indicate no more inputs should be produced.
  bool stopped_ = true;

  // Input generation and management variables.
  CorpusPtr seed_corpus_;
  CorpusPtr live_corpus_;
  Mutagen mutagen_;

  // Queue of generated inputs for a workflow that are consumed by |TestInputs|.
  AsyncDeque<Input> generated_;

  // Interfaces to other components.
  TargetAdapterClient target_adapter_;
  CoverageProviderClient coverage_provider_;

  // Feedback collection and analysis variables.
  ModulePoolPtr pool_;
  std::unordered_map<uint64_t, std::unique_ptr<ProcessProxy>> process_proxies_;

  // A list of futures that include running the target adapter and awaiting errors or completion
  // status from process proxies. This is primarily used within |RunOne|, but needs to be visible
  // outside that method so completion futures for newly added processes can be added to it.
  std::vector<Future<bool, uint64_t>> futures_;
  fpromise::suspended_task suspended_;

  // Queue of tested input for a workflow that are ready to be processed and/or recycled.
  AsyncDeque<Input> processed_;

  Workflow workflow_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(RunnerImpl);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_FRAMEWORK_ENGINE_RUNNER_H_
