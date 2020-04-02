/**
 * Copyright (c) 2014-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <chrono>

#include <fbzmq/zmq/Zmq.h>
#include <folly/Memory.h>
#include <folly/Optional.h>

#include <openr/if/gen-cpp2/KvStore_constants.h>
#include <openr/if/gen-cpp2/KvStore_types.h>
#include <openr/kvstore/KvStore.h>
#include <openr/messaging/ReplicateQueue.h>

namespace openr {

/**
 * A utility class to wrap and interact with KvStore. It exposes the APIs to
 * send commands to and receive publications from KvStore.
 * Mainly used for testing.
 *
 * Not thread-safe, use from the same thread only.
 */
class KvStoreWrapper {
 public:
  KvStoreWrapper(
      fbzmq::Context& zmqContext,
      std::string nodeId,
      std::chrono::seconds dbSyncInterval,
      std::chrono::seconds monitorSubmitInterval,
      std::unordered_map<std::string, thrift::PeerSpec> peers,
      std::optional<KvStoreFilters> filters = std::nullopt,
      KvStoreFloodRate kvstoreRate = std::nullopt,
      std::chrono::milliseconds ttlDecr = Constants::kTtlDecrement,
      bool enableFloodOptimization = false,
      bool isFloodRoot = false,
      const std::unordered_set<std::string>& areas =
          {openr::thrift::KvStore_constants::kDefaultArea()},
      std::optional<messaging::RQueue<thrift::PeerUpdateRequest>>
          peerUpdatesQueue = std::nullopt);

  ~KvStoreWrapper() {
    stop();
  }

  /**
   * Synchronous APIs to run and stop KvStore. This creates a thread
   * and stop it on destruction.
   *
   * Synchronous => function call with return only after thread is
   *                running/stopped completely.
   */
  void run() noexcept;
  void stop();

  /**
   * Get reader for KvStore updates queue
   */
  messaging::RQueue<thrift::Publication>
  getReader() {
    return kvStoreUpdatesQueue_.getReader();
  }

  void
  openQueue() {
    kvStoreUpdatesQueue_.open();
  }

  void
  closeQueue() {
    kvStoreUpdatesQueue_.close();
  }

  /**
   * APIs to set key-value into the KvStore. Returns true on success else
   * returns false.
   */
  bool setKey(
      std::string key,
      thrift::Value value,
      std::optional<std::vector<std::string>> nodeIds = std::nullopt,
      std::string area = openr::thrift::KvStore_constants::kDefaultArea());

  /**
   * API to retrieve an existing key-value from KvStore. Returns empty if none
   * exists.
   */
  std::optional<thrift::Value> getKey(
      std::string key,
      std::string area = openr::thrift::KvStore_constants::kDefaultArea());

  /**
   * APIs to set key-values into the KvStore. Returns true on success else
   * returns false.
   */
  bool setKeys(
      const std::vector<std::pair<std::string, thrift::Value>>& keyVals,
      std::optional<std::vector<std::string>> nodeIds = std::nullopt,
      std::string area = openr::thrift::KvStore_constants::kDefaultArea());

  /**
   * API to get dump from KvStore.
   * if we pass a prefix, only return keys that match it
   */
  std::unordered_map<std::string /* key */, thrift::Value> dumpAll(
      std::optional<KvStoreFilters> filters = std::nullopt,
      std::string area = openr::thrift::KvStore_constants::kDefaultArea());

  /**
   * API to get dump hashes from KvStore.
   * if we pass a prefix, only return keys that match it
   */
  std::unordered_map<std::string /* key */, thrift::Value> dumpHashes(
      std::string const& prefix = "",
      std::string area = openr::thrift::KvStore_constants::kDefaultArea());

  /**
   * API to get key vals on which hash differs from provided keyValHashes.
   */
  std::unordered_map<std::string /* key */, thrift::Value> syncKeyVals(
      thrift::KeyVals const& keyValHashes,
      std::string area = openr::thrift::KvStore_constants::kDefaultArea());

  /**
   * API to listen for a publication on PUB queue.
   */
  thrift::Publication recvPublication();

  /**
   * API to get counters from KvStore directly
   */
  fbzmq::thrift::CounterMap getCounters();

  /*
   * Get flooding topology information
   */
  thrift::SptInfos getFloodTopo(
      std::string area = openr::thrift::KvStore_constants::kDefaultArea());

  /**
   * APIs to manage (add/remove) KvStore peers. Returns true on success else
   * returns false.
   */
  bool addPeer(
      std::string peerName,
      thrift::PeerSpec spec,
      std::string area = openr::thrift::KvStore_constants::kDefaultArea());
  bool delPeer(
      std::string peerName,
      std::string area = openr::thrift::KvStore_constants::kDefaultArea());

  /**
   * APIs to get existing peers of a KvStore.
   */
  std::unordered_map<std::string /* peerName */, thrift::PeerSpec> getPeers(
      std::string area = openr::thrift::KvStore_constants::kDefaultArea());

  /**
   * Utility function to get peer-spec for owned KvStore
   */
  thrift::PeerSpec
  getPeerSpec() const {
    return createPeerSpec(globalCmdUrl, enableFloodOptimization_);
  }

  KvStore*
  getKvStore() {
    return kvStore_.get();
  }

 public:
  /**
   * Socket URLs and other constants which can be used for connecting to KvStore
   * sockets.
   */
  const std::string nodeId;

  /**
   * Global URLs could be created outside of kvstore, mainly for testing
   */
  const std::string globalCmdUrl;

  /**
   * Socket URL for zmq Monitoring
   */
  const std::string monitorSubmitUrl;

 private:
  // Thrift serializer object for serializing/deserializing of thrift objects
  // to/from bytes
  apache::thrift::CompactSerializer serializer_;

  // Queue for streaming KvStore updates
  messaging::ReplicateQueue<thrift::Publication> kvStoreUpdatesQueue_;
  messaging::RQueue<thrift::Publication> kvStoreUpdatesQueueReader_{
      kvStoreUpdatesQueue_.getReader()};

  // Queue for streaming peer updates from LM
  messaging::ReplicateQueue<thrift::PeerUpdateRequest> dummyPeerUpdatesQueue_;

  // KvStore owned by this wrapper.
  std::unique_ptr<KvStore> kvStore_;

  // Thread in which KvStore will be running.
  std::thread kvStoreThread_;

  // enable flood optimization or not
  const bool enableFloodOptimization_{false};
};

} // namespace openr
