/*
 * Copyright (c) 2014-present, Facebook, Inc.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

namespace cpp2 openr.thrift
namespace go openr.OpenrConfig
namespace py openr.OpenrConfig
namespace py3 openr.thrift
namespace wiki Open_Routing.Thrift_APIs.OpenrConfig

include "BgpConfig.thrift"

exception ConfigError {
  1: string message
} ( message = "message" )

struct KvstoreFloodRate {
  1: i32 flood_msg_per_sec
  2: i32 flood_msg_burst_size
}

struct KvstoreConfig {
  # kvstore
  1: i32 key_ttl_ms = 300000 # 5min 300*1000
  2: i32 sync_interval_s = 60
  3: i32 ttl_decrement_ms = 1
  4: optional KvstoreFloodRate flood_rate

  5: optional bool set_leaf_node
  6: optional list<string> key_prefix_filters
  7: optional list<string> key_originator_id_filters

  # flood optimization
  8: optional bool enable_flood_optimization
  9: optional bool is_flood_root
}

struct LinkMonitorConfig {
  1: i32 linkflap_initial_backoff_ms = 60000 # 60s
  2: i32 linkflap_max_backoff_ms = 300000 # 5min
  3: bool use_rtt_metric = true

  // below fields are deprecated, prefer area config
  4: list<string> include_interface_regexes = [] (deprecated)
  5: list<string> exclude_interface_regexes = [] (deprecated)
  6: list<string> redistribute_interface_regexes = [] (deprecated)
}

struct StepDetectorConfig {
   1: i64 fast_window_size = 10
   2: i64 slow_window_size = 60
   3: i32 lower_threshold = 2
   4: i32 upper_threshold = 5
   5: i64 ads_threshold = 500
}

struct SparkConfig {
  1: i32 neighbor_discovery_port = 6666

  2: i32 hello_time_s = 20
  3: i32 fastinit_hello_time_ms = 500

  4: i32 keepalive_time_s = 2
  5: i32 hold_time_s = 10
  6: i32 graceful_restart_time_s = 30

  7: StepDetectorConfig step_detector_conf
}

struct WatchdogConfig {
  1: i32 interval_s = 20
  2: i32 thread_timeout_s = 300
  3: i32 max_memory_mb = 800
}

struct MonitorConfig {
  1: i32 max_event_log = 100
  2: bool enable_event_log_submission  = true
}

enum PrefixForwardingType {
  IP = 0
  SR_MPLS = 1
}

enum PrefixForwardingAlgorithm {
  SP_ECMP = 0
  KSP2_ED_ECMP = 1
}

/*
 * DYNAMIC_LEAF_NODE
 *   => looks for seed_prefix in kvstore and elects a subprefix
 * DYNAMIC_ROOT_NODE
 *   => elects subprefix from configured seed_prefix
 * STATIC
 *   => looks for static allocation key in kvstore and use the prefix
 */
enum PrefixAllocationMode {
  DYNAMIC_LEAF_NODE = 0
  DYNAMIC_ROOT_NODE = 1
  STATIC = 2
}

struct PrefixAllocationConfig {
  1: string loopback_interface = "lo"
  2: bool set_loopback_addr = false
  3: bool override_loopback_addr = false

  // If prefixAllocationMode == DYNAMIC_ROOT_NODE
  // seed_prefix and allocate_prefix_len needs to be filled.
  4: PrefixAllocationMode prefix_allocation_mode
  5: optional string seed_prefix
  6: optional i32 allocate_prefix_len
}

// describes prefixes for route origination purpose
struct OriginatedPrefix {
  1: string prefix

  // Default mode of forwarding for prefix is IP.
  2: PrefixForwardingType forwardingType = PrefixForwardingType.IP

  # Default forwarding (route computation) algorithm is shortest path ECMP.
  3: PrefixForwardingAlgorithm forwardingAlgorithm =
    PrefixForwardingAlgorithm.SP_ECMP

  // minimum # of supporting routes to advertise this prefix.
  // Prefix will be advertised/withdrawn when # of supporting routes change.
  4: i16 minimum_supporting_routes

  // flag to indicate FIB programming
  5: optional bool install_to_fib

  6: optional i32 source_preference

  7: optional i32 path_preference

  // Set of tags associated with this route. This is meta-data and intends to be
  // used by Policy. NOTE: There is no ordering on tags
  8: optional set<string> tags

  // to interact with BGP, area prepending is needed
  9: optional list<string> area_stack

  // If the # of nethops for this prefix is below certain threshold, Decision
  // will not program/anounce the routes. If this parameter is not set, Decision
  // will not do extra check # of nexthops.
  11: optional i64 minNexthop
}

/**
 * The area config specifies the area name, interfaces to perform discovery
 * on, neighbor names to peer with, and interface addresses to redistribute
 *
 * 1) Config specifying patricular interface patterns and all neighbors. Will
 *    perform discovery on interfaces matching any include pattern and no
 *    exclude pattern and peer with any node in this area:
 *
 *  config = {
 *    area_id : "MyNetworkArea",
 *    neighbor_regexes : [".*"],
 *    include_interface_regexes : ["ethernet1", "port-channel.*"],
 *    exclude_interface_regexes : ["loopback1"],
 *    redistribute_interface_regexes: ["loopback10"],
 *  }
 *
 *
 * 2) Config specifying particular neighbor patterns and all interfaces. Will
 *    perform discovery on all available interfaces and peer with nodes in
 *    this area matching any neighbor pattern:
 *
 *  config = {
 *    area_id : "MyNetworkArea",
 *    neighbor_regexes : ["node123", "fsw.*"],
 *    include_interface_regexes : [".*"],
 *    exclude_interface_regexes : [],
 *    redistribute_interface_regexes: ["loopback10"],
 *  }
 *
 *
 * 3) Config specifying both. Will perform discovery on interfaces matching
 *    any include pattern and no exclude pattern and peer with nodes in this
 *    area matching any neighbor pattern:
 *
 *  config = {
 *    area_id : "MyNetworkArea",
 *    neighbor_regexes : ["node1.*"],
 *    include_interface_regexes : ["loopback0"],
 *    exclude_interface_regexes : ["loopback1"],
 *    redistribute_interface_regexes: ["loopback10"],
 *  }
 *
 *
 * 4) Config specifying neither. Will perform discovery on no interfaces and
 *    peer with no nodes:
 *
 *  config = {
 *    area_id : "MyNetworkArea",
 *    neighbor_regexes : [],
 *    include_interface_regexes : [],
 *    exclude_interface_regexes : [],
 *    redistribute_interface_regexes: ["loopback10"],
 *  }
 *
 *
 */
struct AreaConfig {
  1: string area_id
  3: list<string> neighbor_regexes
  4: list<string> include_interface_regexes
  5: list<string> exclude_interface_regexes

  // will advertise addresses on interfaces matching these patterns into
  // this area
  6: list<string> redistribute_interface_regexes
}

/**
 * Configuration to facilitate route translation between BGP <-> OpenR
 * - BGP Communities <=> tags
 * - BGP AS Path <=> area_stack
 * - BGP Origin <=> metrics.path_preference (IGP=1000, EGP=900, INCOMPLETE=500)
 * - BGP Special Source AS => metrics.source_preference
 * - BGP AS Path Length => metrics.distance
 * - metrics.source_preference => BGP Local Preference
 */
struct BgpRouteTranslationConfig {
  /**
   * Map that defines communities in '{asn}:{value}' format to their human
   * readable names. Open/R will use the community name as tag
   * If not available then string representation of community will be used as
   * tag
   */
  1: map<string, string> communities_to_name;

  /**
   * Map that defines ASN to Area name mapping. Mostly for readability. Open/R
   * will use this mapping to convert as-path to area_stack and vice versa.
   * If not mapping is found then string representation of ASN will be used.
   */
  2: map<i64, string> asn_to_area;

  /**
   * Source preference settings
   * The ASN if specified will add `5 * (3 - #count_asn)` to source preference
   * e.g. if `source_preference_asn = 65000` and AS_PATH contains the 65000 asn
   * twice, then `source_preference = 100 + 5 ( 3 - 2) = 105`
   */
  4: i64 default_source_preference = 100
  5: optional i64 source_preference_asn

  /**
   * ASNs to ignore for distance computation
   */
  6: set<i64> asns_to_ignore_for_distance;

  /**
   * Knob to enable BGP -> Open/R translation incrementally
   */
  7: bool is_enabled = 0 (deprecated) // use 8: enable_bgp_to_openr

  /**
   * Knob to enable BGP -> Open/R translation incrementally
   */
  8: bool enable_bgp_to_openr = 0

  /**
   * Knob to enable Open/R -> BGP translation incrementally
   */
  9: bool enable_openr_to_bgp = 0

}

struct OpenrConfig {
  1: string node_name
  # domain is deprecated, prefer area config
  2: string domain (deprecated)
  3: list<AreaConfig> areas = []

  # Thrift Server - Bind dddress and port
  4: string listen_addr = "::"
  5: i32 openr_ctrl_port = 2018

  6: optional bool dryrun
  7: optional bool enable_v4
  # do route programming through netlink
  8: optional bool enable_netlink_fib_handler

  # Hard wait time before decision start to compute routes
  # if not set, first neighbor update will trigger route computation
  10: optional i32 eor_time_s

  11: PrefixForwardingType prefix_forwarding_type = PrefixForwardingType.IP
  12: PrefixForwardingAlgorithm prefix_forwarding_algorithm =
    PrefixForwardingAlgorithm.SP_ECMP
  13: optional bool enable_segment_routing
  14: optional i32 prefix_min_nexthop

  # Config for different modules
  15: KvstoreConfig kvstore_config
  16: LinkMonitorConfig link_monitor_config
  17: SparkConfig spark_config

  # Watchdog
  18: optional bool enable_watchdog
  19: optional WatchdogConfig watchdog_config

  # Prefix allocation
  20: optional bool enable_prefix_allocation
  21: optional PrefixAllocationConfig prefix_allocation_config

  # Fib
  22: optional bool enable_ordered_fib_programming
  23: i32 fib_port

  # Enables `RibPolicy` for computed routes. This knob allows thrift APIs to
  # set/get `RibPolicy` in Decision module. For more information refer to
  # `struct RibPolicy` in OpenrCtrl.thrift
  # Disabled by default
  24: bool enable_rib_policy = 0

  # Config for monitor module
  25: MonitorConfig monitor_config

  # KvStore thrift migration flags
  # TODO: the following flags serve as rolling-out purpose
  26: bool enable_kvstore_thrift = 1
  27: bool enable_periodic_sync = 1

  # V4/V6 prefixes to be originated
  31: optional list<OriginatedPrefix> originated_prefixes

  # Flag for enabling best route selection based on PrefixMetrics
  # TODO: This is temporary & will go away once new prefix metrics is rolled out
  51: bool enable_best_route_selection = 0

  # Maximum hold time for synchronizing the prefixes in KvStore after service
  # starts up. It is expected that all the sources inform PrefixManager about
  # the routes to be advertised within the hold time window. PrefixManager
  # can choose to synchronize routes as soon as it receives END marker from
  # all the expected sources.
  52: i32 prefix_hold_time_s = 15

  # Delay in seconds for MPLS route deletes. The delay would allow the remote
  # nodes to converge to new prepend-label associated with route advertisement.
  # This will avoid packet drops because of label route lookup
  # NOTE: Label route add or update will happen immediately
  53: i32 mpls_route_delete_delay_s = 10

  # Feature gate for new graceful restart behavior
  # New workflow is to promote adj up after kvstore reaches initial sync state
  # This is a series of new changes in turn to address traffic loss during
  # agent cold boot
  54: bool enable_new_gr_behavior = 0

  # bgp
  100: optional bool enable_bgp_peering
  102: optional BgpConfig.BgpConfig bgp_config

  # Configuration to facilitate Open/R <-> BGP route conversions.
  # NOTE: This must be specified if bgp_peering is enabled
  104: optional BgpRouteTranslationConfig bgp_translation_config
}
