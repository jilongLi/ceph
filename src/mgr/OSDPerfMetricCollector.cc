// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#include "common/debug.h"
#include "common/errno.h"

#include "OSDPerfMetricCollector.h"

#define dout_context g_ceph_context
#define dout_subsys ceph_subsys_mgr
#undef dout_prefix
#define dout_prefix *_dout << "mgr.osd_perf_metric_collector " << __func__ << " "

OSDPerfMetricCollector::OSDPerfMetricCollector(Listener &listener)
  : listener(listener), lock("OSDPerfMetricCollector::lock") {
}

std::list<OSDPerfMetricQuery> OSDPerfMetricCollector::get_queries() {
  std::lock_guard locker(lock);

  std::list<OSDPerfMetricQuery> query_list;
  for (auto &it : queries) {
    query_list.push_back(it.first);
  }

  return query_list;
}

int OSDPerfMetricCollector::add_query(const OSDPerfMetricQuery& query) {
  uint64_t query_id;
  bool notify = false;

  {
    std::lock_guard locker(lock);

    query_id = next_query_id++;
    auto it = queries.find(query);
    if (it == queries.end()) {
      it = queries.insert({query, {}}).first;
      notify = true;
    }
    it->second.insert(query_id);
  }

  dout(10) << query << " query_id=" << query_id << dendl;

  if (notify) {
    listener.handle_query_updated();
  }

  return query_id;
}

int OSDPerfMetricCollector::remove_query(int query_id) {
  bool found = false;
  bool notify = false;

  {
    std::lock_guard locker(lock);

    for (auto it = queries.begin() ; it != queries.end(); it++) {
      auto &ids = it->second;

      if (ids.erase(query_id) > 0) {
        if (ids.empty()) {
          queries.erase(it);
          notify = true;
        }
        found = true;
        break;
      }
    }
  }

  if (!found) {
    dout(10) << query_id << " not found" << dendl;
    return -ENOENT;
  }

  dout(10) << query_id << dendl;

  if (notify) {
    listener.handle_query_updated();
  }

  return 0;
}

void OSDPerfMetricCollector::remove_all_queries() {
  dout(10) << dendl;

  bool notify;

  {
    std::lock_guard locker(lock);

    notify = !queries.empty();
    queries.clear();
  }

  if (notify) {
    listener.handle_query_updated();
  }
}

void OSDPerfMetricCollector::process_reports(
    const std::map<OSDPerfMetricQuery, OSDPerfMetricReport> &reports) {

  if (reports.empty()) {
    return;
  }

  std::lock_guard locker(lock);

  for (auto &it : reports) {
    auto &report = it.second;
    dout(10) << "report for " << it.first << " query: "
             << report.group_packed_performance_counters.size() << " records"
             << dendl;
    for (auto &it : report.group_packed_performance_counters) {
      auto &key = it.first;
      auto bl_it = it.second.cbegin();
      for (auto &d : report.performance_counter_descriptors) {
        PerformanceCounter c;
        d.unpack_counter(bl_it, &c);
        dout(20) << "counter " << key << " " << d << ": " << c << dendl;
      }
    }
  }
}
