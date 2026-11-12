/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
 * Author: GreenSocs 2022
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _GREENSOCS_BASE_COMPONENTS_ROUTER_H
#define _GREENSOCS_BASE_COMPONENTS_ROUTER_H

#include <cinttypes>
#include <utility>
#include <vector>
#include <map>
#include <set>           // Required for std::set
#include <limits>        // Required for std::numeric_limits
#include <memory>        // Required for std::shared_ptrs
#include <unordered_map> // Required for std::unordered_map
#include <list>          // Required for std::list
#include <functional>    // Required for std::less

#define THREAD_SAFE true
#if defined(THREAD_SAFE) and THREAD_SAFE
#include <mutex>
#include <shared_mutex>
#endif
#include <atomic>

#include <cci_configuration>
#include <systemc>
#include <tlm>
#include <scp/report.h>
#include <scp/helpers.h>
#include <cciutils.h>

#include <tlm_utils/multi_passthrough_initiator_socket.h>
#include <tlm_utils/multi_passthrough_target_socket.h>
#include <tlm-extensions/pathid_extension.h>
#include <tlm-extensions/underlying-dmi.h>

#include <router_if.h>
#include <module_factory_registery.h>
#include <tlm_sockets_buswidth.h>

namespace gs {

template <typename Key, typename Value>
class AddrMapCacheBase
{
public:
    virtual ~AddrMapCacheBase() = default;
    virtual bool get(const Key& key, Value& value) = 0;
    virtual void put(const Key& key, const Value& value, uint64_t size) = 0;
    virtual void clear() = 0;

    // Statistics interface
    virtual uint64_t get_hits() const = 0;
    virtual uint64_t get_misses() const = 0;
    virtual void reset_stats() = 0;
};

/**
 * @brief AddrMapNoCache - A cache implementation that never caches (always misses).
 *
 */
template <typename Key, typename Value>
class AddrMapNoCache : public AddrMapCacheBase<Key, Value>
{
public:
    /// @brief Always returns false (cache miss)
    bool get(const Key& key, Value& value) override
    {
        (void)key;   // Suppress unused parameter warning
        (void)value; // Suppress unused parameter warning
        return false;
    }

    /// @brief Does nothing (no caching)
    void put(const Key& key, const Value& value, [[maybe_unused]] uint64_t size) override
    {
        (void)key;   // Suppress unused parameter warning
        (void)value; // Suppress unused parameter warning
    }

    /// @brief Does nothing (nothing to clear)
    void clear() override {}

    // Statistics interface implementation
    uint64_t get_hits() const override { return 0; }
    uint64_t get_misses() const override { return 0; }
    void reset_stats() override {}
};

/**
 * @class router
 * @brief A SystemC TLM router module for transaction routing based on address.
 *
 * This router connects multiple initiator sockets to multiple target sockets,
 * routing transactions based on the address map configured via CCI parameters.
 * It supports DMI, debug transport, and various TLM extensions.
 *
 * @tparam BUSWIDTH The TLM bus width in bits.
 */
template <unsigned int BUSWIDTH = DEFAULT_TLM_BUSWIDTH, template <typename, typename> class CacheType = AddrMapNoCache>
class router : public sc_core::sc_module, public gs::router_if<BUSWIDTH>
{
    /// @brief Type alias for TLM target socket.
    using TargetSocket = tlm::tlm_base_target_socket_b<BUSWIDTH, tlm::tlm_fw_transport_if<>,
                                                       tlm::tlm_bw_transport_if<>>;
    /// @brief Type alias for TLM initiator socket.
    using InitiatorSocket = tlm::tlm_base_initiator_socket_b<BUSWIDTH, tlm::tlm_fw_transport_if<>,
                                                             tlm::tlm_bw_transport_if<>>;
    /// @brief Type alias for target_info from the router_if base class.
    using typename gs::router_if<BUSWIDTH>::target_info;
    /// @brief Type alias for multi-passthrough initiator socket with spying capability.
    using initiator_socket_type = typename gs::router<
        BUSWIDTH, CacheType>::template multi_passthrough_initiator_socket_spying<router<BUSWIDTH, CacheType>>;
    /// @brief Access to bound_targets from the base class.
    using gs::router_if<BUSWIDTH>::bound_targets;

    using CacheImpl = CacheType<uint64_t, std::shared_ptr<target_info>>;
    /**
     * @class addressMap
     * @brief High-performance address map with automatic region splitting and priority resolution.
     *
     * This implementation provides O(log n) address lookups using a single std::map with automatic
     * region splitting to handle overlapping memory regions. When overlapping regions are added,
     * the map automatically splits them into non-overlapping segments and resolves conflicts
     * based on priority (lower numeric value = higher priority).
     *
     * Key features:
     * - Single unified map instead of multiple priority-based maps
     * - Automatic region splitting with priority-based conflict resolution
     * - LRU cache for frequently accessed addresses
     * - No external dependencies (boost-free implementation)
     * - Thread-safe design compatible with SystemC simulation
     *
     * @tparam TargetInfoType The type of target information to store (typically target_info)
     */
    template <typename TargetInfoType>
    class addressMap
    {
    private:
        /**
         * @brief Represents a contiguous address region mapped to a specific target.
         *
         * Each region covers the address range [start, end) with exclusive end address.
         * Regions are non-overlapping after the splitting process.
         */
        struct Region {
            uint64_t start;                         ///< Start address (inclusive)
            uint64_t end;                           ///< End address (exclusive)
            std::shared_ptr<TargetInfoType> target; ///< Target information

            Region(uint64_t s, uint64_t e, std::shared_ptr<TargetInfoType> t): start(s), end(e), target(t) {}
        };

        /// @brief Map of regions keyed by start address for O(log n) lookups
        std::map<uint64_t, Region> m_regions;

        CacheImpl m_cache;

        /**
         * @brief Split and resolve overlapping regions using priority-based conflict resolution.
         *
         * This is the core algorithm that handles overlapping memory regions. It works by:
         * 1. Finding all existing regions that overlap with the new region
         * 2. Creating a unified list of all intervals (new + existing)
         * 3. Using boundary-based segmentation to split overlapping areas
         * 4. Resolving conflicts by priority (lower number = higher priority)
         * 5. Creating non-overlapping regions in the final map
         *
         * Time complexity: O(k log k) where k is the number of overlapping regions
         * Space complexity: O(k) for temporary data structures
         *
         * @param new_start Start address of the new region
         * @param new_end End address of the new region (exclusive)
         * @param new_target Target information for the new region
         */
        void split_and_resolve(uint64_t new_start, uint64_t new_end, std::shared_ptr<TargetInfoType> new_target)
        {
            // Step 1: Collect all existing regions that overlap with the new region
            std::vector<Region> overlapping_regions;
            std::vector<uint64_t> keys_to_remove;

            // Find overlapping regions using interval overlap test: (a.start < b.end && a.end > b.start)
            for (const auto& [start_addr, region] : m_regions) {
                if (region.start < new_end && region.end > new_start) {
                    overlapping_regions.push_back(region);
                    keys_to_remove.push_back(start_addr);
                }
            }

            // Step 2: Remove overlapping regions from the map (they'll be re-added after splitting)
            for (uint64_t key : keys_to_remove) {
                m_regions.erase(key);
            }

            // Step 3: Create intervals for all regions that need to be resolved
            struct Interval {
                uint64_t start, end;
                std::shared_ptr<TargetInfoType> target;
                uint32_t priority;

                Interval(uint64_t s, uint64_t e, std::shared_ptr<TargetInfoType> t)
                    : start(s), end(e), target(t), priority(t->priority)
                {
                }
            };

            std::vector<Interval> intervals;
            intervals.reserve(overlapping_regions.size() + 1); // Optimize allocation

            // Add the new region
            intervals.emplace_back(new_start, new_end, new_target);

            // Add all overlapping existing regions
            for (const Region& region : overlapping_regions) {
                intervals.emplace_back(region.start, region.end, region.target);
            }

            // Step 4: Create sorted list of all boundary points for segmentation
            std::set<uint64_t> boundaries;
            for (const Interval& interval : intervals) {
                boundaries.insert(interval.start);
                boundaries.insert(interval.end);
            }

            // Step 5: Process each segment between boundaries and resolve conflicts
            auto boundary_it = boundaries.begin();
            while (boundary_it != boundaries.end()) {
                uint64_t seg_start = *boundary_it;
                ++boundary_it;
                if (boundary_it == boundaries.end()) break;
                uint64_t seg_end = *boundary_it;

                // Find the highest priority target for this segment
                std::shared_ptr<TargetInfoType> best_target = nullptr;
                uint32_t best_priority = UINT32_MAX;

                for (const Interval& interval : intervals) {
                    // Check if this interval covers the current segment
                    if (interval.start <= seg_start && interval.end >= seg_end) {
                        if (interval.priority < best_priority) {
                            best_priority = interval.priority;
                            best_target = interval.target;
                        }
                    }
                }

                // Add the resolved segment to the final map
                if (best_target) {
                    m_regions.emplace(seg_start, Region(seg_start, seg_end, best_target));
                }
            }

            // Step 6: Check for completely shadowed regions and issue warnings
            for (const Interval& interval : intervals) {
                // Skip the newly added region (it can't be shadowed by itself)
                if (interval.start == new_start && interval.end == new_end && interval.target == new_target) {
                    continue;
                }

                // Check if this interval is completely covered by higher priority regions
                // We do this by checking if any part of this interval "wins" in the final mapping
                bool completely_shadowed = true;

                // Look through the final resolved regions to see if any belong to this interval's target
                for (const auto& [seg_start, region] : m_regions) {
                    // Check if this final region overlaps with our interval and belongs to our target
                    if (region.target == interval.target && region.start < interval.end &&
                        region.end > interval.start) {
                        // This interval has at least some part that's not shadowed
                        completely_shadowed = false;
                        break;
                    }
                }

                if (completely_shadowed) {
                    std::stringstream ss;
                    ss << "Region '" << interval.target->name << "' (0x" << std::hex << interval.start << "-0x"
                       << (interval.end - 1) << ", priority " << std::dec << interval.priority
                       << ") is completely shadowed by higher priority regions and will never be accessed";
                    SC_REPORT_WARNING("addressMap", ss.str().c_str());
                }
            }
        }

    public:
        /// @brief Default constructor - creates an empty address map
        addressMap() = default;

        /**
         * @brief Add a new target region with automatic splitting and priority resolution.
         *
         * This method adds a new memory region to the address map. If the new region
         * overlaps with existing regions, the split_and_resolve algorithm automatically
         * handles the conflict resolution based on priority values.
         *
         * After adding a region, the LRU cache is cleared to ensure consistency,
         * as the address mapping may have changed.
         *
         * Time complexity: O(k log k) where k is the number of overlapping regions
         *
         * @param t_info Shared pointer to target information containing address, size, and priority
         */
        void add(std::shared_ptr<TargetInfoType> t_info)
        {
            uint64_t start = t_info->address;
            uint64_t end = t_info->address + t_info->size;

            split_and_resolve(start, end, t_info);

            // Clear cache after map modification to ensure consistency
            m_cache.clear();
        }

        /**
         * @brief Find target by address with high-performance LRU caching.
         *
         * This method performs address-to-target resolution with two-level lookup:
         * 1. First checks the LRU cache for O(1) performance on repeated accesses
         * 2. Falls back to O(log n) map lookup if not cached
         *
         * The cache significantly improves performance for workloads with spatial
         * or temporal locality in memory access patterns.
         *
         * @param address The address to look up
         * @return Shared pointer to target info if found, nullptr if address is unmapped
         */
        std::shared_ptr<TargetInfoType> find(uint64_t address, uint64_t size)
        {
            // Fast path: check LRU cache first
            std::shared_ptr<TargetInfoType> cached_result;
            if (m_cache.get(address, cached_result)) {
                return cached_result;
            }

            // Slow path: search the map using upper_bound for O(log n) lookup
            // upper_bound finds the first region with start > address
            auto it = m_regions.upper_bound(address);
            if (it != m_regions.begin()) {
                --it; // Move to the region that might contain our address

                // Check if address falls within this region [start, end)
                if (address >= it->second.start && address < it->second.end) {
                    // Cache the result for future lookups
                    m_cache.put(address, it->second.target, size);
                    return it->second.target;
                }
            }

            // Address not found - cache the negative result to avoid repeated lookups
            m_cache.put(address, nullptr, size);
            return nullptr;
        }

        /**
         * @brief Find region boundaries for Direct Memory Interface (DMI) operations.
         *
         * This method determines the valid address range for DMI access. It handles
         * two cases:
         * 1. Mapped regions: Returns the full extent of the original memory region
         * 2. Unmapped regions (holes): Returns the boundaries of the address hole
         *
         * For mapped regions, it's important to return the original memory boundaries
         * rather than the split segment boundaries, as DMI clients expect to access
         * the full memory region.
         *
         * @param addr The address to query for DMI boundaries
         * @param dmi Reference to tlm_dmi object to populate with boundary information
         * @return Shared pointer to target info if address is mapped, nullptr for holes
         */
        std::shared_ptr<TargetInfoType> find_region(uint64_t addr, tlm::tlm_dmi& dmi)
        {
            // First try to find a mapped region using the same logic as find()
            auto it = m_regions.upper_bound(addr);
            if (it != m_regions.begin()) {
                --it;
                if (addr >= it->second.start && addr < it->second.end) {
                    // Return the segment boundaries. After split_and_resolve, adjacent
                    // same-target segments are merged, so this segment represents the
                    // full contiguous range owned by this target. We must not use the
                    // original target range as it may span across higher-priority targets.
                    std::shared_ptr<TargetInfoType> target = it->second.target;
                    dmi.set_start_address(it->second.start);
                    dmi.set_end_address(it->second.end - 1); // inclusive end (Region.end is exclusive)
                    return target;
                }
            }

            // Address is in a hole - calculate hole boundaries for DMI invalidation
            uint64_t hole_start = 0;
            uint64_t hole_end = std::numeric_limits<uint64_t>::max();

            // Find the region immediately before this address
            it = m_regions.upper_bound(addr);
            if (it != m_regions.begin()) {
                --it;
                if (it->second.end <= addr) {
                    hole_start = it->second.end;
                }
            }

            // Find the region immediately after this address
            it = m_regions.upper_bound(addr);
            if (it != m_regions.end()) {
                hole_end = it->second.start;
            }

            // Set DMI boundaries for the hole
            dmi.set_start_address(hole_start);
            dmi.set_end_address(hole_end == 0 ? 0 : hole_end - 1); // Handle edge case
            return nullptr;
        }

        /**
         * @brief Get cache statistics (hits and misses).
         * @param hits Output parameter for number of cache hits
         * @param misses Output parameter for number of cache misses
         */
        void get_cache_stats(uint64_t& hits, uint64_t& misses) const
        {
            hits = m_cache.get_hits();
            misses = m_cache.get_misses();
        }

        /**
         * @brief Reset cache statistics to zero.
         */
        void reset_cache_stats() { m_cache.reset_stats(); }
    };

    SCP_LOGGER_VECTOR(D);
    SCP_LOGGER(());
    SCP_LOGGER((DMI), "dmi");

private:
    /// @brief Structure to hold DMI information and associated initiators.
    struct dmi_info {
        std::set<int> initiators; ///< @brief Set of initiator IDs holding this DMI.
        tlm::tlm_dmi dmi;         ///< @brief The DMI region.
        /// @brief Constructor for dmi_info.
        dmi_info(tlm::tlm_dmi& _dmi) { dmi = _dmi; }
    };
    /// @brief Map of DMI information, keyed by start address.
    std::map<uint64_t, dmi_info> m_dmi_info_map;

    /**
     * @brief Checks if DMI info is in cache or inserts it.
     *
     * This function attempts to find an existing DMI entry for the given DMI object.
     * If found, it checks for address range consistency. If not found, it inserts
     * a new dmi_info entry into the map.
     *
     * @param dmi The TLM DMI object.
     * @return A pointer to the dmi_info object in the map.
     */
    dmi_info* in_dmi_cache(tlm::tlm_dmi& dmi)
    {
        auto it = m_dmi_info_map.find(dmi.get_start_address());
        if (it != m_dmi_info_map.end()) {
            if (it->second.dmi.get_end_address() != dmi.get_end_address()) {
                // Defensive guard: unreachable in normal operation. The only caller
                // (record_dmi) detects differing end addresses and calls
                // invalidate_direct_mem_ptr_ts to erase the old entry before reaching here.
                std::stringstream ss;
                ss << "Can't handle that: DMI overlap with differing end address (0x" << std::hex
                   << it->second.dmi.get_end_address() << " vs 0x" << dmi.get_end_address() << ")";
                SC_REPORT_ERROR("DMI", ss.str().c_str());
            }
            return &(it->second);
        }
        auto insit = m_dmi_info_map.emplace(dmi.get_start_address(), dmi_info(dmi));
        return &(insit.first->second);
    }

    /**
     * @brief Records DMI information for an initiator.
     *
     * This function adds the given initiator ID to the set of initiators associated
     * with the DMI region. It also handles potential overlaps with existing DMI regions,
     * invalidating old ones if necessary.
     *
     * @param id The initiator ID.
     * @param dmi The TLM DMI object to record.
     */
    void record_dmi(int id, tlm::tlm_dmi& dmi)
    {
        auto it = m_dmi_info_map.find(dmi.get_start_address());
        if (it != m_dmi_info_map.end()) {
            if (it->second.dmi.get_end_address() != dmi.get_end_address()) {
                SCP_WARN((DMI)) << "A new DMI overlaps with an old one, invalidating the old one";
                invalidate_direct_mem_ptr_ts(0, dmi.get_start_address(),
                                             dmi.get_end_address()); // id will be ignored
            }
        }

        dmi_info* dinfo = in_dmi_cache(dmi);
        dinfo->initiators.insert(id);
    }

    /**
     * @brief Registers a newly bound target socket.
     *
     * This method is called when a target socket is bound to the router's initiator_socket.
     * It creates a new `target_info` object as a `shared_ptr`, populates its initial details,
     * and adds it to the `bound_targets` vector.
     *
     * @param s The name of the bound socket.
     */
    void register_boundto(std::string s) override
    {
        s = gs::router_if<BUSWIDTH>::nameFromSocket(s);
        // Create a shared_ptr for the new target_info
        std::shared_ptr<target_info> ti_ptr = std::make_shared<target_info>();
        ti_ptr->name = s;
        ti_ptr->index = bound_targets.size(); // Current size will be its index
        SCP_LOGGER_VECTOR_PUSH_BACK(D, ti_ptr->name);
        SCP_DEBUG((D[ti_ptr->index])) << "Connecting : " << ti_ptr->name;
        ti_ptr->chained = false;
        std::string tmp = name();
        int i;
        for (i = 0; i < tmp.length(); i++)
            if (s[i] != tmp[i]) break;
        ti_ptr->shortname = s.substr(i);
        bound_targets.push_back(ti_ptr); // Add shared_ptr to bound_targets
    }

    /**
     * @brief Converts a TLM generic payload into a human-readable string for logging.
     *
     * This utility function formats the command, address, data length, data (if applicable),
     * and response status of a TLM transaction into a string.
     *
     * @param ti Pointer to the target_info associated with the transaction (const pointer).
     * @param trans The TLM generic payload.
     * @return A string representation of the transaction.
     */
    std::string txn_tostring(const target_info* ti, tlm::tlm_generic_payload& trans)
    {
        std::stringstream info;
        const char* cmd = "UNKOWN";
        switch (trans.get_command()) {
        case tlm::TLM_IGNORE_COMMAND:
            info << "IGNORE ";
            break;
        case tlm::TLM_WRITE_COMMAND:
            info << "WRITE ";
            break;
        case tlm::TLM_READ_COMMAND:
            info << "READ ";
            break;
        }

        info << " address:" << "0x" << std::hex << trans.get_address();
        info << " len:" << trans.get_data_length();
        unsigned char* ptr = trans.get_data_ptr();
        if ((trans.get_command() == tlm::TLM_READ_COMMAND && trans.get_response_status() == tlm::TLM_OK_RESPONSE) ||
            (trans.get_command() == tlm::TLM_WRITE_COMMAND &&
             trans.get_response_status() == tlm::TLM_INCOMPLETE_RESPONSE)) {
            info << " data:0x";
            for (int i = trans.get_data_length(); i; i--) {
                info << std::setw(2) << std::setfill('0') << std::hex << (unsigned int)(ptr[i - 1]);
            }
        }
        info << " " << trans.get_response_string() << " ";
        for (int i = 0; i < tlm::max_num_extensions(); i++) {
            if (trans.get_extension(i)) {
                info << " extn:" << i;
            }
        }
        return info.str();
    }

public:
    /// @brief Initiator socket to connect to targets.
    initiator_socket_type initiator_socket;
    /// @brief Target socket to receive transactions from initiators.
    tlm_utils::multi_passthrough_target_socket<router<BUSWIDTH, CacheType>, BUSWIDTH> target_socket;
    /// @brief CCI broker handle for configuration parameters.
    cci::cci_broker_handle m_broker;

private:
    /// @brief Stores shared_ptrs to `target_info` objects for aliases.
    std::vector<std::shared_ptr<target_info>> alias_targets;
    /// @brief The address map for routing transactions.
    addressMap<target_info> m_address_map;
    /// @brief Stores shared_ptrs to `target_info` objects, indexed by initiator ID.
    std::vector<std::shared_ptr<target_info>> id_targets;

    /// @brief Pool of PathIDExtension objects to reuse.
    std::vector<PathIDExtension*> m_pathIDPool;
#if defined(THREAD_SAFE) and THREAD_SAFE
    /// @brief Mutex for protecting the PathIDExtension pool in a thread-safe environment.
    std::mutex m_pool_mutex;
#endif

    /**
     * @brief Stamps a TLM generic payload with an initiator ID using a PathIDExtension.
     *
     * If no PathIDExtension is present, one is taken from the pool (or created if empty)
     * and added to the transaction. The initiator ID is then pushed onto the extension's path.
     *
     * @param id The initiator ID.
     * @param txn The TLM generic payload to stamp.
     */
    void stamp_txn(int id, tlm::tlm_generic_payload& txn)
    {
        PathIDExtension* ext = nullptr;
        txn.get_extension(ext);
        if (ext == nullptr) {
#if defined(THREAD_SAFE) and THREAD_SAFE
            std::lock_guard<std::mutex> l(m_pool_mutex);
#endif
            if (m_pathIDPool.size() == 0) {
                ext = new PathIDExtension();
            } else {
                ext = m_pathIDPool.back();
                m_pathIDPool.pop_back();
            }
            txn.set_extension(ext);
        }
        ext->push_back(id);
    }

    /**
     * @brief Unstamps a TLM generic payload by removing the last initiator ID from its PathIDExtension.
     *
     * The initiator ID is popped from the extension's path. If the path becomes empty, the extension
     * is returned to the pool for reuse.
     *
     * @param id The initiator ID to remove.
     * @param txn The TLM generic payload to unstamp.
     */
    void unstamp_txn(int id, tlm::tlm_generic_payload& txn)
    {
        PathIDExtension* ext;
        txn.get_extension(ext);
        assert(ext);
        assert(ext->back() == id);
        ext->pop_back();
        if (ext->size() == 0) {
#if defined(THREAD_SAFE) and THREAD_SAFE
            std::lock_guard<std::mutex> l(m_pool_mutex);
#endif
            txn.clear_extension(ext);
            m_pathIDPool.push_back(ext);
        }
    }

    /**
     * @brief Implements the blocking transport method for TLM transactions.
     *
     * This method decodes the transaction address to find the appropriate target.
     * It then stamps the transaction with the initiator ID, adjusts the address if
     * the target uses an offset, forwards the transaction to the target, and finally
     * unstamps the transaction.
     *
     * @param id The ID of the initiator socket.
     * @param trans The TLM generic payload to transport.
     * @param delay Current time delay for the transaction.
     */
    void b_transport(int id, tlm::tlm_generic_payload& trans, sc_core::sc_time& delay)
    {
        sc_dt::uint64 addr = trans.get_address();
        auto ti = decode_address(trans);
        if (!ti) {
            SCP_WARN(())("Attempt to access unknown register at offset 0x{:x}", addr);
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return;
        }

        stamp_txn(id, trans);
        struct transaction_guard {
            router& owner;
            tlm::tlm_generic_payload& txn;
            const sc_dt::uint64 address;
            const int id;
            ~transaction_guard()
            {
                txn.set_address(address);
                owner.unstamp_txn(id, txn);
            }
        } guard{ *this, trans, addr, id };
        if (!ti->chained) {
            SCP_TRACE((D[ti->index]), ti->name) << "Start b_transport :" << txn_tostring(ti.get(), trans);
        }
        if (trans.get_response_status() >= tlm::TLM_INCOMPLETE_RESPONSE) {
            if (ti->use_offset) trans.set_address(addr - ti->address);

            initiator_socket[ti->index]->b_transport(trans, delay);

            if (ti->use_offset) trans.set_address(addr);
        }
        if (!ti->chained) {
            SCP_TRACE((D[ti->index]), ti->name) << "Completed b_transport :" << txn_tostring(ti.get(), trans);
        }
    }

    /**
     * @brief Implements the debug transport method for TLM transactions.
     *
     * This method decodes the transaction address to find the appropriate target.
     * It then adjusts the address if the target uses an offset, and forwards
     * the debug transaction to the target.
     *
     * @param id The ID of the initiator socket.
     * @param trans The TLM generic payload for debug transport.
     * @return The number of bytes debug-transported.
     */
    unsigned int transport_dbg(int id, tlm::tlm_generic_payload& trans)
    {
        // Ensure router is initialized (thread-safe)
        lazy_initialize();

        sc_dt::uint64 addr = trans.get_address();
        struct address_guard {
            tlm::tlm_generic_payload& txn;
            const sc_dt::uint64 address;
            ~address_guard() { txn.set_address(address); }
        } guard{ trans, addr };
        auto ti = decode_address(trans);
        if (!ti) {
            trans.set_response_status(tlm::TLM_ADDRESS_ERROR_RESPONSE);
            return 0;
        }

        if (ti->use_offset) trans.set_address(addr - ti->address);
        SCP_TRACE((D[ti->index]), ti->name) << "calling dbg_transport : " << scp::scp_txn_tostring(trans);
        unsigned int ret = initiator_socket[ti->index]->transport_dbg(trans);
        if (ti->use_offset) trans.set_address(addr);
        return ret;
    }

    /**
     * @brief Implements the get_direct_mem_ptr method for TLM DMI.
     *
     * This method attempts to retrieve a Direct Memory Interface (DMI) pointer
     * for a given transaction address. It finds the target and calculates the DMI
     * hole, handles mutex locking for DMI operations, and records the DMI info.
     *
     * @param id The ID of the initiator socket.
     * @param trans The TLM generic payload requesting DMI.
     * @param dmi_data Reference to the tlm_dmi object to populate with DMI information.
     * @return True if a DMI pointer was successfully retrieved, false otherwise.
     */
    bool get_direct_mem_ptr(int id, tlm::tlm_generic_payload& trans, tlm::tlm_dmi& dmi_data)
    {
        // Ensure router is initialized (thread-safe)
        lazy_initialize();

        sc_dt::uint64 addr = trans.get_address();

        tlm::tlm_dmi dmi_data_hole;
        auto ti = m_address_map.find_region(addr, dmi_data_hole);

        UnderlyingDMITlmExtension* u_dmi;
        trans.get_extension(u_dmi);
        if (u_dmi) {
            SCP_DEBUG(())
            ("DMI info 0x{:x} 0x{:x} {}", dmi_data_hole.get_start_address(), dmi_data_hole.get_end_address(),
             (ti ? "mapped" : "nomap"));
            u_dmi->add_dmi(this, dmi_data_hole, (ti ? gs::tlm_dmi_ex::dmi_mapped : gs::tlm_dmi_ex::dmi_nomap));
        }

        if (!ti) {
            return false;
        }
        if (ti->use_offset) trans.set_address(addr - ti->address);
        SCP_TRACE((D[ti->index]), ti->name) << "calling get_direct_mem_ptr : " << scp::scp_txn_tostring(trans);
        bool status = initiator_socket[ti->index]->get_direct_mem_ptr(trans, dmi_data);
        if (ti->use_offset) trans.set_address(addr);
        if (status) {
            if (ti->use_offset) {
                assert(dmi_data.get_start_address() < ti->size);
                dmi_data.set_start_address(ti->address + dmi_data.get_start_address());
                dmi_data.set_end_address(ti->address + dmi_data.get_end_address());
            }
            /* ensure we dont overspill the 'hole' we have in the address map */
            if (dmi_data.get_start_address() < dmi_data_hole.get_start_address()) {
                dmi_data.set_dmi_ptr(dmi_data.get_dmi_ptr() +
                                     (dmi_data_hole.get_start_address() - dmi_data.get_start_address()));
                dmi_data.set_start_address(dmi_data_hole.get_start_address());
            }
            if (dmi_data.get_end_address() > dmi_data_hole.get_end_address()) {
                dmi_data.set_end_address(dmi_data_hole.get_end_address());
            }
            record_dmi(id, dmi_data);
        }
        SCP_DEBUG(())
        ("Providing DMI (status {:x}) {:x} - {:x}", status, dmi_data.get_start_address(), dmi_data.get_end_address());
        return status;
    }

    /**
     * @brief Invalidates DMI pointers for a specific initiator within a given address range.
     *
     * This method is called by the initiator socket to signal that cached DMI pointers
     * for a region are no longer valid. It uses `invalidate_direct_mem_ptr_ts` for the actual invalidation.
     *
     * @param id The ID of the initiator socket.
     * @param start The start address of the invalidated region.
     * @param end The end address of the invalidated region.
     */
    void invalidate_direct_mem_ptr(int id, sc_dt::uint64 start, sc_dt::uint64 end)
    {
        if (id_targets[id]->use_offset) {
            start = id_targets[id]->address + start;
            end = id_targets[id]->address + end;
        }
        invalidate_direct_mem_ptr_ts(id, start, end);
    }

    /**
     * @brief Thread-safe implementation to invalidate DMI pointers.
     *
     * This function iterates through the stored DMI regions, identifies overlaps with the
     * invalidated range, and collects all initiator IDs associated with these regions.
     * It then erases the invalidated DMI entries and notifies the affected target sockets.
     *
     * @param id The ID of the initiator socket (used for logging, often ignored for global invalidation).
     * @param start The start address of the invalidated region.
     * @param end The end address of the invalidated region.
     */
    void invalidate_direct_mem_ptr_ts(int id, sc_dt::uint64 start, sc_dt::uint64 end)
    {
        auto it = m_dmi_info_map.upper_bound(start);

        if (it != m_dmi_info_map.begin()) {
            /*
             * Start with the preceding region, as it may already cross the
             * range we must invalidate.
             */
            it--;
        }
        std::set<int> initiators;

        while (it != m_dmi_info_map.end()) {
            tlm::tlm_dmi& r = it->second.dmi;

            if (r.get_start_address() > end) {
                /* We've got out of the invalidation range */
                break;
            }

            if (r.get_end_address() < start) {
                /* We are not in yet */
                it++;
                continue;
            }
            for (auto t : it->second.initiators) {
                SCP_TRACE((DMI)) << "Queueing initiator " << t << " for invalidation, its bounds are [0x" << std::hex
                                 << it->second.dmi.get_start_address() << " - 0x" << it->second.dmi.get_end_address()
                                 << "]";
                initiators.insert(t);
            }
            it = m_dmi_info_map.erase(it);
        }
        for (auto t : initiators) {
            SCP_INFO((DMI)) << "Invalidating initiator " << t << " [0x" << std::hex << start << " - 0x" << end << "]";
            target_socket[t]->invalidate_direct_mem_ptr(start, end);
        }
    }

protected:
    /**
     * @brief Decodes the address from a TLM generic payload to find the target.
     *
     * This virtual function, overriding the base class, ensures that the router
     * is initialized (via `lazy_initialize`) before attempting to find the
     * `target_info` associated with the transaction's address using the internal `addressMap`.
     *
     * @param trans The TLM generic payload.
     * @return A shared_ptr to the `target_info` object if found, otherwise nullptr.
     */
    std::shared_ptr<target_info> decode_address(tlm::tlm_generic_payload& trans) override
    {
        lazy_initialize();

        sc_dt::uint64 addr = trans.get_address();
        return m_address_map.find(addr, trans.get_data_length());
    }

    /**
     * @brief Called before end of elaboration to ensure lazy initialization.
     *
     * If `lazy_init` is false, this method triggers the `lazy_initialize` process.
     */
    virtual void before_end_of_elaboration() override
    {
        if (!lazy_init) lazy_initialize();
    }

private:
    /// @brief Atomic flag to track if lazy initialization has occurred (thread-safe).
    std::atomic<bool> m_initialized{ false };
    /// @brief Mutex to protect lazy initialization.
#if defined(THREAD_SAFE) and THREAD_SAFE
    std::mutex m_init_mutex;
#endif
    std::list<std::string> get_matching_children(cci::cci_broker_handle broker, const std::string& prefix,
                                                 const std::vector<cci_name_value_pair>& list)
    {
        size_t prefix_len = prefix.length() + 1; // +1 for the dot separator
        std::list<std::string> children;
        for (const auto& p : list) {
            if (p.first.find(prefix) == 0) {
                // Extract the child name after the prefix
                std::string child = p.first.substr(prefix_len, p.first.find(".", prefix_len) - prefix_len);
                children.push_back(child);
            }
        }
        children.sort();
        children.unique();
        return children;
    }

    /**
     * @brief Performs lazy initialization of the router's address map (thread-safe).
     *
     * This method is responsible for configuring the address map based on the
     * `bound_targets` and CCI parameters. It retrieves address, size, offset,
     * chained, and priority information for each target and its aliases,
     * then adds them to the `m_address_map`. This method is an override
     * of the virtual function in `router_if`.
     *
     * Thread-safety: Uses double-checked locking with atomic flag for performance.
     * - Fast path: Lock-free check if already initialized
     * - Slow path: Mutex-protected initialization with double-check
     */
    void lazy_initialize() override
    {
        // Fast path: check if already initialized (lock-free)
        if (m_initialized.load(std::memory_order_acquire)) {
            return;
        }

#if defined(THREAD_SAFE) and THREAD_SAFE
        // Slow path: acquire lock and initialize
        std::lock_guard<std::mutex> lock(m_init_mutex);
#endif

        // Double-check: another thread may have initialized while we waited
        if (m_initialized.load(std::memory_order_relaxed)) {
            return;
        }

        // Perform initialization
        // Get filtered range and convert to vector to materialize the results
        auto all_alias_range = m_broker.get_unconsumed_preset_values([](const std::pair<std::string, cci_value>& iv) {
            return iv.first.find(".aliases.") != std::string::npos;
        });
        std::vector<cci_name_value_pair> all_alias(all_alias_range.begin(), all_alias_range.end());

        for (auto& ti_ptr : bound_targets) {
            std::string name = ti_ptr->name;
            std::string src;
            if (gs::cci_get<std::string>(m_broker, ti_ptr->name + ".0", src)) {
                // deal with an alias
                if (m_broker.has_preset_value(ti_ptr->name + ".address") &&
                    m_broker.get_preset_cci_value(ti_ptr->name + ".address").is_number()) {
                    SCP_WARN((D[ti_ptr->index]), ti_ptr->name)
                    ("The configuration alias provided ({}) will be ignored as a valid address is also provided.", src);
                } else {
                    if (src[0] == '&') src = (src.erase(0, 1)) + ".target_socket";
                    if (!m_broker.has_preset_value(src + ".address")) {
                        std::stringstream ss;
                        ss << "The configuration alias provided (" << src << ") can not be found.";
                        SC_REPORT_ERROR("router", ss.str().c_str());
                    }
                    name = src;
                }
            }

            // Update the target_info object pointed to by ti_ptr
            ti_ptr->address = gs::cci_get<uint64_t>(m_broker, name + ".address");
            ti_ptr->size = gs::cci_get<uint64_t>(m_broker, name + ".size");
            ti_ptr->use_offset = gs::cci_get_d<bool>(m_broker, name + ".relative_addresses", true);
            ti_ptr->chained = gs::cci_get_d<bool>(m_broker, name + ".chained", false);
            ti_ptr->priority = gs::cci_get_d<uint32_t>(m_broker, name + ".priority", 0);

            SCP_INFO((D[ti_ptr->index]), ti_ptr->name)
                << "Address map " << ti_ptr->name << " at address " << "0x" << std::hex << ti_ptr->address << " size "
                << "0x" << std::hex << ti_ptr->size << (ti_ptr->use_offset ? " (with relative address) " : " ")
                << "priority : " << ti_ptr->priority;
            if (ti_ptr->chained) SCP_DEBUG(())("{} is chained so debug will be suppressed", ti_ptr->name);

            // Add the shared_ptr to the address map
            m_address_map.add(ti_ptr);
            for (std::string n : get_matching_children(m_broker, (ti_ptr->name + ".aliases"), all_alias)) {
                std::string alias_name = ti_ptr->name + ".aliases." + n;
                uint64_t address = gs::cci_get<uint64_t>(m_broker, alias_name + ".address");
                uint64_t size = gs::cci_get<uint64_t>(m_broker, alias_name + ".size");
                SCP_INFO((D[ti_ptr->index]), ti_ptr->name)
                ("Adding alias {} {:#x} (size: {})", alias_name, address, size);

                // Create a new shared_ptr for the alias, copying from the current ti_ptr state
                std::shared_ptr<target_info> alias_ti_ptr = std::make_shared<target_info>(*ti_ptr);
                alias_ti_ptr->address = address;
                alias_ti_ptr->size = size;
                alias_ti_ptr->name = alias_name;
                alias_targets.push_back(alias_ti_ptr); // Store shared_ptr in alias_targets

                // Add aliases to the address map
                m_address_map.add(alias_ti_ptr);
            }
            id_targets.push_back(ti_ptr); // Store shared_ptr in id_targets
        }

        // Mark as initialized (release semantics ensures all writes are visible)
        m_initialized.store(true, std::memory_order_release);
    }

    /// @brief CCI parameter to control lazy initialization.
    cci::cci_param<bool> lazy_init;

    /**
     * @brief Constructor for the router module.
     *
     * Initializes the base sc_module, initiator and target sockets, address map,
     * CCI broker, and the lazy initialization parameter. Registers transport methods.
     *
     * @param nm SystemC module name.
     * @param broker Optional CCI broker handle. Defaults to the global broker.
     */
public:
    explicit router(const sc_core::sc_module_name& nm, cci::cci_broker_handle broker = cci::cci_get_broker())
        : sc_core::sc_module(nm)
        , initiator_socket("initiator_socket", [&](std::string s) -> void { register_boundto(s); })
        , target_socket("target_socket")
        , m_address_map()
        , m_broker(broker)
        , lazy_init("lazy_init", false, "Initialize the router lazily (eg. during simulation rather than BEOL)")
    {
        SCP_DEBUG(()) << "router constructed";

        target_socket.register_b_transport(this, &router<BUSWIDTH, CacheType>::b_transport);
        target_socket.register_transport_dbg(this, &router<BUSWIDTH, CacheType>::transport_dbg);
        target_socket.register_get_direct_mem_ptr(this, &router<BUSWIDTH, CacheType>::get_direct_mem_ptr);
        initiator_socket.register_invalidate_direct_mem_ptr(this,
                                                            &router<BUSWIDTH, CacheType>::invalidate_direct_mem_ptr);
        SCP_DEBUG((DMI)) << "router Initializing DMI SCP reporting";
    }

    /// @brief Deleted default constructor to enforce named instantiation.
    router() = delete;

    /// @brief Deleted copy constructor.
    router(const router&) = delete;

public:
    /// @brief Destructor for the router module.
    /// Cleans up any remaining PathIDExtension objects in the pool.
    ~router()
    {
        for (auto* extension : m_pathIDPool) delete extension;
    }

    /**
     * @brief Adds a target to the router's address map.
     *
     * This method configures CCI parameters for the target (address, size, relative_addresses)
     * and binds the target socket to the router's initiator socket.
     *
     * @param t The target socket to add.
     * @param address The base address of the target.
     * @param size The size of the target's address range.
     * @param masked Boolean indicating if relative addresses should be used (defaults to true).
     * @param priority unsigned int priority, lower values have higher priorities
     */
    void add_target(TargetSocket& t, const uint64_t address, uint64_t size, bool masked = true,
                    unsigned int priority = 0)
    {
        std::string s = gs::router_if<BUSWIDTH>::nameFromSocket(t.get_base_export().name());
        if (!m_broker.has_preset_value(s + ".address")) {
            m_broker.set_preset_cci_value(s + ".address", cci::cci_value(address));
        }
        if (!m_broker.has_preset_value(s + ".size")) {
            m_broker.set_preset_cci_value(s + ".size", cci::cci_value(size));
        }
        if (!m_broker.has_preset_value(s + ".relative_addresses")) {
            m_broker.set_preset_cci_value(s + ".relative_addresses", cci::cci_value(masked));
        }
        if (!m_broker.has_preset_value(s + ".priority")) {
            SCP_DEBUG(())("Setting prio to {}", priority);
            m_broker.set_preset_cci_value(s + ".priority", cci::cci_value(priority));
        }
        initiator_socket.bind(t);
    }

    /**
     * @brief Adds an initiator to the router.
     *
     * This method handles the binding between the initiator's port and the router's
     * target socket.
     *
     * @param i The initiator socket to add.
     */
    virtual void add_initiator(InitiatorSocket& i)
    {
        // hand bind the port/exports as we are using base classes
        (i.get_base_port())(target_socket.get_base_interface());
        (target_socket.get_base_port())(i.get_base_interface());
    }

    /**
     * @brief Get cache statistics (hits and misses).
     *
     * This method exposes the cache performance metrics for benchmarking and analysis.
     *
     * @param hits Output parameter for number of cache hits
     * @param misses Output parameter for number of cache misses
     */
    void get_cache_stats(uint64_t& hits, uint64_t& misses) const { m_address_map.get_cache_stats(hits, misses); }

    /**
     * @brief Reset cache statistics to zero.
     *
     * This method should be called before each benchmark iteration to ensure
     * accurate measurement of cache performance for that specific test.
     */
    void reset_cache_stats() { m_address_map.reset_cache_stats(); }
};
} // namespace gs

extern "C" void module_register();
#endif
