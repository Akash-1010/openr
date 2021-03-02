/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <folly/fibers/Semaphore.h>
#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/AsyncTimeout.h>
#include <folly/io/async/EventBase.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include <openr/common/ExponentialBackoff.h>
#include <openr/common/OpenrEventBase.h>
#include <openr/common/Util.h>
#include <openr/config/Config.h>
#include <openr/decision/RouteUpdate.h>
#include <openr/if/gen-cpp2/FibService.h>
#include <openr/if/gen-cpp2/Platform_types.h>
#include <openr/if/gen-cpp2/Types_types.h>
#include <openr/kvstore/KvStoreClientInternal.h>
#include <openr/messaging/Queue.h>
#include <openr/monitor/LogSample.h>

namespace openr {

/**
 * Proxy agent to program computed routes using platform dependent agent (e.g.
 * FBOSS in case of Wedge Platform).
 *
 * The functionality is very simple. We just react to RouteDatabase updates
 * from Decision module and forward best paths to switch agent to program.
 * There is no state keeping being done apart from handling interface events.
 *
 * This RouteDatabase contains all Loop Free Alternate (LFAs) paths along with
 * best paths. So Fib module derives best paths (path with minimum cost) and
 * programs them.
 *
 * Note: If for a prefix there are multiple paths with the smallest cost then
 * we program all of them which simulates ECMP behaviours across programmed
 * nexthops.
 *
 */
class Fib final : public OpenrEventBase {
 public:
  Fib(std::shared_ptr<const Config> config,
      int32_t thriftPort,
      std::chrono::seconds coldStartDuration,
      messaging::RQueue<DecisionRouteUpdate> routeUpdatesQueue,
      messaging::RQueue<DecisionRouteUpdate> staticRoutesUpdateQueue,
      messaging::ReplicateQueue<DecisionRouteUpdate>& fibUpdatesQueue,
      messaging::ReplicateQueue<LogSample>& logSampleQueue,
      KvStore* kvStore);

  /**
   * Override stop method of OpenrEventBase
   */
  void stop() override;

  /**
   * Utility function to create thrift client connection to SwitchAgent. Can
   * throw exception if it fails to open transport to client on specified port.
   * It will return immediately if healthy client connection already exists.
   */
  static void createFibClient(
      folly::EventBase& evb,
      std::shared_ptr<folly::AsyncSocket>& socket,
      std::unique_ptr<thrift::FibServiceAsyncClient>& client,
      int32_t port);

  /**
   * Perform longest prefix match among all prefixes in route database.
   * @param inputPrefix - a prefix that need to be matched
   * @param unicastRoutes - current unicast routes in RouteDatabase
   *
   * @return the matched CIDRNetwork if prefix matching succeed.
   */
  static std::optional<folly::CIDRNetwork> longestPrefixMatch(
      const folly::CIDRNetwork& inputPrefix,
      const std::unordered_map<folly::CIDRNetwork, thrift::UnicastRouteDetail>&
          unicastRoutes);

  /**
   * Show unicast routes which are to be added or updated
   */
  static void printUnicastRoutesAddUpdate(
      const std::vector<thrift::UnicastRoute>& unicastRoutesToUpdate);

  /**
  Show MPLS routes which are to be added or updated
  */
  static void printMplsRoutesAddUpdate(
      const std::vector<thrift::MplsRoute>& mplsRoutesToUpdate);

  /**
   * NOTE: DEPRECATED! Use getUnicastRoutes or getMplsRoutes.
   */
  folly::SemiFuture<std::unique_ptr<thrift::RouteDatabase>> getRouteDb();

  folly::SemiFuture<std::unique_ptr<thrift::RouteDatabaseDetail>>
  getRouteDetailDb();

  /**
   * Retrieve unicast routes for specified prefixes or IP. Returns all if
   * no prefix is specified in filter list.
   */
  folly::SemiFuture<std::unique_ptr<std::vector<thrift::UnicastRoute>>>
  getUnicastRoutes(std::vector<std::string> prefixes);

  /**
   * Retrieve mpls routes for specified labels. Returns all if no label is
   * specified in filter list.
   */
  folly::SemiFuture<std::unique_ptr<std::vector<thrift::MplsRoute>>>
  getMplsRoutes(std::vector<int32_t> labels);

  /**
   * Retrieve performance related information from FIB module
   */
  folly::SemiFuture<std::unique_ptr<thrift::PerfDatabase>> getPerfDb();

  /**
   * API to get reader for fibUpdatesQueue
   */
  messaging::RQueue<DecisionRouteUpdate> getFibUpdatesReader();

 private:
  // No-copy
  Fib(const Fib&) = delete;
  Fib& operator=(const Fib&) = delete;

  /**
   * Convert local perfDb_ into PerfDataBase
   */
  thrift::PerfDatabase dumpPerfDb() const;

  /**
   * Retrieve unicast routes with specified filters
   */
  std::vector<thrift::UnicastRoute> getUnicastRoutesFiltered(
      std::vector<std::string> prefixes);

  /**
   * Retrieve mpls routes with specified filters
   */
  std::vector<thrift::MplsRoute> getMplsRoutesFiltered(
      std::vector<int32_t> labels);

  /**
   * Process new route updates received from Decision module
   */
  void processRouteUpdates(DecisionRouteUpdate&& routeUpdate);

  /**
   * Trigger add/del routes thrift calls
   * on success no action needed
   * on failure invokes syncRouteDbDebounced
   */
  void updateRoutes(DecisionRouteUpdate&& routeUpdate, bool isStaticRoutes);

  /**
   * Sync the current routeDb_ with the switch agent.
   * on success no action needed
   * on failure invokes syncRouteDbDebounced
   */
  bool syncRouteDb();

  /**
   * Asynchrounsly schedules the syncRouteDb call and returns immediately. All
   * APIs should call this function to sync-routes.
   */
  void syncRouteDbDebounced();

  /**
   * Get aliveSince from FibService, and check if Fib restarts
   * If so, push syncFib to FibService
   */
  void keepAliveCheck();

  // set flat counter/stats
  void updateGlobalCounters();

  // log perf events
  void logPerfEvents(std::optional<thrift::PerfEvents>& perfEvents);

  // Prefix to available nexthop information. Also store perf information of
  // received route-db if provided.
  struct RouteState {
    // Non modified copy of Unicast and MPLS routes received from Decision
    std::unordered_map<folly::CIDRNetwork, thrift::UnicastRouteDetail>
        unicastRoutes;
    std::unordered_map<uint32_t, thrift::MplsRouteDetail> mplsRoutes;

    // indicates we've received a decision route publication and therefore have
    // routes to sync. will not synce routes with system until this is set
    bool hasRoutesFromDecision{false};

    // Flag to indicate the result of previous route programming attempt.
    // If set, it means what currently cached in local routes has not been 100%
    // successfully synced with agent, we have to trigger an enforced full fib
    // sync with agent again
    bool dirtyRouteDb{false};
  };
  RouteState routeState_;

  // Events to capture and indicate performance of protocol convergence.
  std::deque<thrift::PerfEvents> perfDb_;

  // Create timestamp of recently logged perf event
  int64_t recentPerfEventCreateTs_{0};

  // Name of node on which OpenR is running
  const std::string myNodeName_;

  // Switch agent thrift server port
  const int32_t thriftPort_;

  // In dry run we do not make actual thrift call to manipulate routes
  bool dryrun_{true};

  // Enable segment routing
  bool enableSegmentRouting_{false};

  // indicates that we should publish fib programming time to kvstore
  bool enableOrderedFib_{false};

  apache::thrift::CompactSerializer serializer_;

  // Thrift client connection to switch FIB Agent using which we actually
  // manipulate routes.
  folly::EventBase evb_;
  std::shared_ptr<folly::AsyncSocket> socket_{nullptr};
  std::unique_ptr<thrift::FibServiceAsyncClient> client_{nullptr};

  // Callback timer to sync routes to switch agent and scheduled on route-sync
  // failure. ExponentialBackoff timer to ease up things if they go wrong
  std::unique_ptr<folly::AsyncTimeout> syncRoutesTimer_{nullptr};
  ExponentialBackoff<std::chrono::milliseconds> expBackoff_;

  // periodically send alive msg to switch agent
  std::unique_ptr<folly::AsyncTimeout> keepAliveTimer_{nullptr};

  // module ptr to refer to KvStore for KvStoreClientInternal usage
  KvStore* kvStore_{nullptr};
  std::unique_ptr<KvStoreClientInternal> kvStoreClient_;

  // Queues to publish fib updates (Fib streaming)
  messaging::ReplicateQueue<DecisionRouteUpdate>& fibUpdatesQueue_;

  // Latest aliveSince heard from FibService. If the next one is different then
  // it means that FibAgent has restarted and we need to perform sync.
  int64_t latestAliveSince_{0};

  // moves to true after initial sync
  bool hasSyncedFib_{false};

  const int16_t kFibId_{static_cast<int16_t>(thrift::FibClient::OPENR)};

  // TODO: Remove if not needed
  // Semaphore to serialize route programming across two fibers (interface
  // updates & route updates)
  // NOTE: Initializing with a single slot to avoid parallel processing
  folly::fibers::Semaphore updateRoutesSemaphore_{1};

  // Queue to publish the event log
  messaging::ReplicateQueue<LogSample>& logSampleQueue_;
};

} // namespace openr
