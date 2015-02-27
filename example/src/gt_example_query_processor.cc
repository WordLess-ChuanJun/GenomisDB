#include <iostream>
#include "command_line.h"
#include "loader.h"
#include "query_processor.h"
#include<math.h>
#include "gt_common.h"


#ifdef DO_PROFILING
#include "gperftools/profiler.h"
#endif

void GenotypeColumn(QueryProcessor& qp, QueryProcessor::GTProfileStats* stats, const StorageManager::ArrayDescriptor* ad_gVCF,
    uint64_t column, std::ostream& output_stream)
{
  /*Get one column from array*/ 
  QueryProcessor::GTColumn* gt_column = qp.gt_get_column(ad_gVCF, column, stats);
  //Do dummy genotyping operation
  do_dummy_genotyping(gt_column, output_stream);
  delete gt_column;
}

int main(int argc, char** argv) {
  CommandLineOpts cl;
  parse_command_line(argc, argv, cl);
  if(cl.m_workspace == 0 || (cl.m_position == 0ull && !(cl.m_positions_list.is_open()) && !cl.m_do_scan) || cl.m_array_name == 0)
  {
    std::cerr << "Missing workspace|[position or scan]|array_name\n";
    exit(-1);
  }
  // Create storage manager
  // The input is the path to its workspace (the path must exist).
  StorageManager sm(cl.m_workspace);

  // Open arrays in READ mode
  const StorageManager::ArrayDescriptor* ad_gVCF = 
    sm.open_array(cl.m_array_name);

  std::ostream& output_stream = cl.m_output_fstream.is_open() ? cl.m_output_fstream : std::cout;
  if(cl.m_do_scan)
  {
    // Create query processor
    // The first input is the path to its workspace (the path must exist).
    QueryProcessor qp(cl.m_workspace, sm);
    qp.scan_and_operate(ad_gVCF, output_stream);
  }
  else
  {
    //Compute "optimal" segment size
    const ArraySchema& array_schema = ad_gVCF->array_schema();
    const std::vector<std::pair<double, double> >& dim_domains =
      array_schema.dim_domains();
    uint64_t total_num_samples = dim_domains[0].second - dim_domains[0].first + 1;
    //double expected_num_tiles_per_query =  0.92 + 0.005*total_num_samples;
    //if(expected_num_tiles_per_query < 1)
    //expected_num_tiles_per_query = 1;
    double expected_num_tiles_per_query = 1;
    uint64_t segment_size = expected_num_tiles_per_query*40000;
    //uint64_t segment_size = 8192;
    if(segment_size > SM_SEGMENT_SIZE)
      segment_size = SM_SEGMENT_SIZE;
    //Create new objects
    StorageManager sm_opt(cl.m_workspace, segment_size);
    const StorageManager::ArrayDescriptor* ad_gVCF_opt = sm_opt.open_array(cl.m_array_name);
    // Create query processor
    // The first input is the path to its workspace (the path must exist).
    QueryProcessor qp(cl.m_workspace, sm_opt);
    //Stats struct
    QueryProcessor::GTProfileStats stats;
    if(cl.m_positions_list.is_open())
    {
      uint64_t num_queries = 0;
#ifdef DO_PROFILING
      ProfilerStart("gprofile.log");
      sm_opt.m_coords_attribute_idx = GVCF_COORDINATES_IDX;
#endif
      while(1)
      {
	uint64_t position;
	cl.m_positions_list >> position;
	if(cl.m_positions_list.bad() || cl.m_positions_list.eof() || cl.m_positions_list.fail())
	  break;
	GenotypeColumn(qp, &stats, ad_gVCF_opt, position, output_stream);
	++num_queries;
      }
#ifdef DO_PROFILING
      ProfilerStop();
      if(num_queries)
      {
	printf("mean #cells for first sample,mean #cells per sample,mean #cells for exit,mean #tiles per query,mean #deref tile iters per query,,std-dev #cells for first sample,std-dev #cells per sample,std-dev #cells for exit,std-dev #tiles per query,std-dev #deref tile iters per query,,mean #disk derefs,mean #coords disk deref,mean #cached deref,mean #coords cached deref,mean #tiles loaded from disk\n");
	double mean_cells_per_sample = ((double)stats.m_sum_num_cells_touched)/stats.m_num_samples;
	double stddev_cells_per_sample = sqrt(((double)stats.m_sum_sq_num_cells_touched)/stats.m_num_samples - 
	    (mean_cells_per_sample*mean_cells_per_sample));
	double mean_cells_first_sample = ((double)stats.m_sum_num_cells_first_sample)/num_queries;
	double stddev_cells_first_sample = sqrt(((double)stats.m_sum_sq_num_cells_first_sample)/num_queries - 
	    (mean_cells_first_sample*mean_cells_first_sample));
	double mean_cells_last_iter = ((double)stats.m_sum_num_cells_last_iter)/num_queries;
	double stddev_cells_last_iter = sqrt(((double)stats.m_sum_sq_num_cells_last_iter)/num_queries - mean_cells_last_iter*mean_cells_last_iter);
	double mean_deref_tile_iters = ((double)stats.m_sum_num_deref_tile_iters)/num_queries;
	double stddev_deref_tile_iters = sqrt(((double)stats.m_sum_sq_num_deref_tile_iters)/num_queries - (mean_deref_tile_iters*mean_deref_tile_iters));
	double mean_tiles_per_query = ((double)stats.m_sum_num_tiles_touched)/num_queries;
	double stddev_tiles_per_query = sqrt(((double)stats.m_sum_sq_num_tiles_touched)/num_queries - mean_tiles_per_query*mean_tiles_per_query);
	double mean_disk_loads_per_query = ((double)g_num_disk_loads)/num_queries;
	double mean_coords_disk_loads_per_query = ((double)g_coords_num_disk_loads)/num_queries;
	double mean_cached_loads = ((double)g_num_cached_loads)/num_queries;
	double mean_coords_cached_loads = ((double)g_coords_num_cached_loads)/num_queries;
	double mean_tiles_loaded_from_disk = ((double)g_total_num_tiles_loaded)/num_queries;
	printf("%.2lf,%.2lf,%2.lf,%.2lf,%.2lf,,%.2lf,%.2lf,%2.lf,%.2lf,%.2lf,,%.2lf,%.2lf,%.2lf,%.2lf,%.2lf\n",
	    mean_cells_first_sample, mean_cells_per_sample,
	    mean_cells_last_iter, mean_tiles_per_query, mean_deref_tile_iters,
	    stddev_cells_first_sample, stddev_cells_per_sample,
	    stddev_cells_last_iter, stddev_tiles_per_query, stddev_deref_tile_iters,
	    mean_disk_loads_per_query, mean_coords_disk_loads_per_query, mean_cached_loads,
	    mean_coords_cached_loads, mean_tiles_loaded_from_disk);
      }
#endif
    }
    else
      GenotypeColumn(qp, &stats, ad_gVCF_opt, cl.m_position, output_stream);
    sm_opt.close_array(ad_gVCF_opt);
  }

  sm.close_array(ad_gVCF);
  if(cl.m_output_fstream.is_open())
    cl.m_output_fstream.close();
  if(cl.m_positions_list.is_open())
    cl.m_positions_list.close();

  return 0;
}
