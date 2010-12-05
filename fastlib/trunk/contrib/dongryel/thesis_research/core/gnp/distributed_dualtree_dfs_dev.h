/** @file distributed_dualtree_dfs_dev.h
 *
 *  @author Dongryeol Lee (dongryel@cc.gatech.edu)
 */

#ifndef CORE_GNP_DISTRIBUTED_DUALTREE_DFS_DEV_H
#define CORE_GNP_DISTRIBUTED_DUALTREE_DFS_DEV_H

#include "core/gnp/distributed_dualtree_dfs.h"
#include "core/gnp/dualtree_dfs.h"
#include "core/table/table.h"
#include "core/table/memory_mapped_file.h"

namespace core {
namespace table {
extern core::table::MemoryMappedFile *global_m_file_;
};
};

template<typename ProblemType>
void core::gnp::DistributedDualtreeDfs<ProblemType>::AllReduce_() {

  // Set the local table.
  std::vector< TableType * > remote_tables(world_->size(), NULL);
  remote_tables[ world_->rank()] = reference_table_->local_table();

  // Pair up processes, and exchange. Right now, the entire local
  // reference tree is exchanged between pairs of processes, but this
  // could be improved later. Also assume that the number of processes
  // is a power of two for the moment. This will be changed later to allow
  // transfer of data at a finer granularity, which means there will
  // be an outer loop over the main all-reduce. This solution also is
  // not topology-aware, so it will be changed later to fit the
  // appropriate network topology.
  int num_rounds = log2(world_->size());
  printf("Number of rounds: %d\n", num_rounds);
  for(int r = 1; r <= num_rounds; r++) {
    int stride = 1 << r;
    int num_tables_in_action = stride >> 1;

    // Exchange with the appropriate process.
    int group_offset = world_->rank() % stride;
    int group_leader = world_->rank() - group_offset;
    int group_end = group_leader + stride - 1;
    int exchange_process_id = group_leader + stride - group_offset - 1;

    // Send the process's own collected tables.
    std::vector<boost::mpi::request> send_requests;
    std::vector<boost::mpi::request> receive_requests;
    send_requests.resize(num_tables_in_action);
    receive_requests.resize(num_tables_in_action);

    printf("Round: %d, num_tables: %d, group leader: %d, group end: %d\n",
           r, num_tables_in_action, group_leader, group_end);

    for(int i = 0; i < num_tables_in_action; i++) {
      int send_id;
      int receive_id;
      if(world_->rank() - group_leader < group_end - world_->rank()) {
        send_id = group_leader + i;
        receive_id = group_leader + i + num_tables_in_action;
      }
      else {
        send_id = group_leader + i + num_tables_in_action;
        receive_id = group_leader + i;
      }
      remote_tables[receive_id] =
        core::table::global_m_file_->Construct<TableType>();
      printf("Process %d is sending Table %d to Process %d\n",
             world_->rank(), send_id, exchange_process_id);
      printf("Process %d is receiving Table %d from Process %d\n",
             world_->rank(), receive_id,
             exchange_process_id);
      send_requests[i] = world_->isend(
                           exchange_process_id, send_id,
                           *(remote_tables[send_id]));
      receive_requests[i] =
        world_->irecv(
          exchange_process_id, receive_id, *(remote_tables[receive_id]));
    }
    boost::mpi::wait_all(send_requests.begin(), send_requests.end());
    boost::mpi::wait_all(receive_requests.begin(), receive_requests.end());

    // Each process calls the independent sets of serial dual-tree dfs
    // algorithms. Further parallelism can be exploited here.


  } // End of the all-reduce loop.

  // Destroy all tables after all computations are done, except for
  // the process's own table.
  for(int i = 0; i < static_cast<int>(remote_tables.size()); i++) {
    if(i != world_->rank()) {
      core::table::global_m_file_->DestroyPtr(remote_tables[i]);
    }
  }
}

template<typename ProblemType>
ProblemType *core::gnp::DistributedDualtreeDfs<ProblemType>::problem() {
  return problem_;
}

template<typename ProblemType>
typename ProblemType::DistributedTableType *
core::gnp::DistributedDualtreeDfs<ProblemType>::query_table() {
  return query_table_;
}

template<typename ProblemType>
typename ProblemType::DistributedTableType *
core::gnp::DistributedDualtreeDfs<ProblemType>::reference_table() {
  return reference_table_;
}

template<typename ProblemType>
void core::gnp::DistributedDualtreeDfs<ProblemType>::ResetStatistic() {
  ResetStatisticRecursion_(query_table_->get_tree(), query_table_);
}

template<typename ProblemType>
void core::gnp::DistributedDualtreeDfs<ProblemType>::Init(
  boost::mpi::communicator *world_in,
  ProblemType &problem_in) {
  world_ = world_in;
  problem_ = &problem_in;
  query_table_ = problem_->query_table();
  reference_table_ = problem_->reference_table();
  ResetStatistic();

  if(query_table_ != reference_table_) {
    ResetStatisticRecursion_(reference_table_->get_tree(), reference_table_);
  }
}

template<typename ProblemType>
void core::gnp::DistributedDualtreeDfs<ProblemType>::Compute(
  const core::metric_kernels::AbstractMetric &metric,
  typename ProblemType::ResultType *query_results) {

  // Allocate space for storing the final results.
  query_results->Init(query_table_->n_entries());

  // Preprocess the global query tree and the local query tree owned
  // by each process.
  PreProcess_(query_table_->get_tree());
  PreProcess_(query_table_->local_table()->get_tree());

  // Preprocess the global reference tree, and the local reference
  // tree owned by each process. This part needs to be fixed so that
  // it does a true bottom-up refinement using an MPI-gather.
  PreProcessReferenceTree_(reference_table_->get_tree());
  PreProcessReferenceTree_(reference_table_->local_table()->get_tree());

  // Figure out each process's work using the global tree. This is
  // done using a naive approach where the global goal is to complete
  // a 2D matrix workspace. This is currently doing an all-reduce type
  // of exchange.
  AllReduce_();

  // Postprocess.
  // PostProcess_(metric, query_table_->get_tree(), query_results);
}

template<typename ProblemType>
void core::gnp::DistributedDualtreeDfs<ProblemType>::ResetStatisticRecursion_(
  typename ProblemType::DistributedTableType::TreeType *node,
  typename ProblemType::DistributedTableType * table) {
  node->stat().SetZero();
  if(node->is_leaf() == false) {
    ResetStatisticRecursion_(node->left(), table);
    ResetStatisticRecursion_(node->right(), table);
  }
}

template<typename ProblemType>
template<typename TemplateTreeType>
void core::gnp::DistributedDualtreeDfs<ProblemType>::PreProcessReferenceTree_(
  TemplateTreeType *rnode) {

  typename ProblemType::StatisticType &rnode_stat = rnode->stat();
  typename ProblemType::DistributedTableType::TreeIterator rnode_it =
    reference_table_->get_node_iterator(rnode);

  if(rnode->is_leaf()) {
    rnode_stat.Init(rnode_it);
  }
  else {

    // Get the left and the right children.
    TemplateTreeType *rnode_left_child = rnode->left();
    TemplateTreeType *rnode_right_child = rnode->right();

    // Recurse to the left and the right.
    PreProcessReferenceTree_(rnode_left_child);
    PreProcessReferenceTree_(rnode_right_child);

    // Build the node stat by combining those owned by the children.
    typename ProblemType::StatisticType &rnode_left_child_stat =
      rnode_left_child->stat();
    typename ProblemType::StatisticType &rnode_right_child_stat =
      rnode_right_child->stat();
    rnode_stat.Init(
      rnode_it, rnode_left_child_stat, rnode_right_child_stat);
  }
}

template<typename ProblemType>
template<typename TemplateTreeType>
void core::gnp::DistributedDualtreeDfs<ProblemType>::PreProcess_(
  TemplateTreeType *qnode) {

  typename ProblemType::StatisticType &qnode_stat = qnode->stat();
  qnode_stat.SetZero();

  if(! qnode->is_leaf()) {
    PreProcess_(qnode->left());
    PreProcess_(qnode->right());
  }
}

#endif
