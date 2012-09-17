/*
 * ssd_statistics_gatherer.cpp
 *
 *  Created on: Jul 19, 2012
 *      Author: niv
 */

#include "../ssd.h"
using namespace ssd;
#include <stdio.h>
#include <sstream>

StatisticsGatherer *StatisticsGatherer::inst = NULL;

StatisticsGatherer::StatisticsGatherer(Ssd& ssd)
	:  num_gc_cancelled_no_candidate(0),
	   num_gc_cancelled_not_enough_free_space(0),
	   num_gc_cancelled_gc_already_happening(0),
	   ssd(ssd),
	  sum_bus_wait_time_for_reads_per_LUN(SSD_SIZE, vector<double>(PACKAGE_SIZE, 0)),
	  num_reads_per_LUN(SSD_SIZE, vector<uint>(PACKAGE_SIZE, 0)),
	  sum_bus_wait_time_for_writes_per_LUN(SSD_SIZE, vector<double>(PACKAGE_SIZE, 0)),
	  num_writes_per_LUN(SSD_SIZE, vector<uint>(PACKAGE_SIZE, 0)),
	  num_gc_reads_per_LUN(SSD_SIZE, vector<uint>(PACKAGE_SIZE, 0)),
	  num_gc_writes_per_LUN_origin(SSD_SIZE, vector<uint>(PACKAGE_SIZE, 0)),
	  num_gc_writes_per_LUN_destination(SSD_SIZE, vector<uint>(PACKAGE_SIZE, 0)),
	  num_gc_scheduled_per_LUN(SSD_SIZE, vector<uint>(PACKAGE_SIZE, 0)),
	  num_executed_gc_ops(SSD_SIZE, vector<uint>(PACKAGE_SIZE, 0)),
	  num_live_pages_in_gc_exec(SSD_SIZE, vector<uint>(PACKAGE_SIZE, 0)),
	  sum_gc_wait_time_per_LUN(SSD_SIZE, vector<double>(PACKAGE_SIZE, 0)),
	  num_copy_backs_per_LUN(SSD_SIZE, vector<uint>(PACKAGE_SIZE, 0)),
	  num_erases_per_LUN(SSD_SIZE, vector<uint>(PACKAGE_SIZE, 0)),
	  num_gc_executed(0),
	  num_migrations(0),
	  num_gc_scheduled(0),
	  num_gc_targeting_package_die_class(0),
	  num_gc_targeting_package_die(0),
	  num_gc_targeting_package_class(0),
	  num_gc_targeting_package(0),
	  num_gc_targeting_class(0),
	  num_gc_targeting_anything(0)

{}

vector<vector<double> > num_valid_pages_per_gc_op;
	vector<vector<int> > num_executed_gc_ops;

StatisticsGatherer::~StatisticsGatherer() {}

void StatisticsGatherer::init(Ssd * ssd)
{
	if (inst != NULL) delete inst;
	inst = new StatisticsGatherer(*ssd);
}

StatisticsGatherer *StatisticsGatherer::get_instance()
{
	return inst;
}

void StatisticsGatherer::register_completed_event(Event const& event) {
	Address a = event.get_address();
	if (event.get_event_type() == WRITE) {
		sum_bus_wait_time_for_writes_per_LUN[a.package][a.die] += event.get_bus_wait_time();
		if (event.is_original_application_io()) {
			num_writes_per_LUN[a.package][a.die]++;
		} else if (event.is_garbage_collection_op()) {
			Address replace_add = event.get_replace_address();
			num_gc_writes_per_LUN_origin[replace_add.package][replace_add.die]++;
			num_gc_writes_per_LUN_destination[a.package][a.die]++;

			sum_gc_wait_time_per_LUN[a.package][a.die] += event.get_bus_wait_time();
		}
	} else if (event.get_event_type() == READ_COMMAND || event.get_event_type() == READ_TRANSFER) {
		sum_bus_wait_time_for_reads_per_LUN[a.package][a.die] += event.get_bus_wait_time();
		if (event.get_event_type() == READ_TRANSFER) {
			if (event.is_original_application_io()) {
				num_reads_per_LUN[a.package][a.die]++;
			} else if (event.is_garbage_collection_op()) {
				num_gc_reads_per_LUN[a.package][a.die]++;
			}
		}
	} else if (event.get_event_type() == ERASE) {
		num_erases_per_LUN[a.package][a.die]++;
	} else if (event.get_event_type() == COPY_BACK) {
		num_copy_backs_per_LUN[a.package][a.die]++;
	}
	wait_time_histogram[floor(event.get_bus_wait_time() / wait_time_histogram_steps)*wait_time_histogram_steps]++;
}

void StatisticsGatherer::register_scheduled_gc(Event const& gc) {
	num_gc_scheduled++;

	int age_class = gc.get_age_class();
	Address addr = gc.get_address();

	if (age_class == UNDEFINED) {
		int i = 0;
		i++;
	}

	if (addr.valid == DIE && age_class != UNDEFINED) {
		num_gc_targeting_package_die_class++;
		num_gc_scheduled_per_LUN[addr.package][addr.die]++;
	}
	else if (addr.valid == PACKAGE && age_class != UNDEFINED) {
		num_gc_targeting_package_class++;
	}
	else if (addr.valid == NONE && age_class != UNDEFINED) {
		num_gc_targeting_class++;
	}
	else if (addr.valid == DIE) {
		num_gc_targeting_package_die++;
		num_gc_scheduled_per_LUN[addr.package][addr.die]++;
	}
	else if (addr.valid == PACKAGE) {
		num_gc_targeting_package++;
	}
	else {
		num_gc_targeting_anything++;
	}
}

void StatisticsGatherer::register_executed_gc(Event const& gc, Block const& victim) {
	num_gc_executed++;
	num_migrations += victim.get_pages_valid();
	Address a = Address(victim.get_physical_address(), BLOCK);
	//num_valid_pages_per_gc_op[gc.get_address().package][gc.get_address().die] += victim.get_pages_valid();
	num_executed_gc_ops[a.package][a.die] += 1;
	num_live_pages_in_gc_exec[a.package][a.die] += victim.get_pages_valid();
}

void StatisticsGatherer::register_events_queue_length(uint queue_size, double time) {
	if (time == 0) return;
	uint current_window = floor(time / queue_length_tracker_resolution);
	while (current_window > 0 && queue_length_tracker.size() < current_window) {
		queue_length_tracker.push_back(queue_length_tracker.back());
//		printf("-> COPIED LAST (vs=%d, window=%d)", queue_length_tracker.size(), current_window);
	}
	if (queue_length_tracker.size() == current_window) {
		queue_length_tracker.push_back(queue_size);
//		printf("Q: %f\t: %d\t (%d)", time, queue_size, queue_length_tracker.size() * queue_length_tracker_resolution);
//		printf("-> SAMPLED");
//		printf("\n");
	}
}

void StatisticsGatherer::print() {
	printf("\n\t");
	printf("num writes\t");
	printf("num reads\t");
	printf("GC writes\t");
	printf("GC reads\t");
	printf("copy backs\t");
	printf("erases\t\t");
	printf("avg write wait\t");
	printf("avg read wait\t");
	//printf("age\t");

	printf("\n");


	uint total_writes = 0;
	uint total_reads = 0;
	uint total_gc_writes = 0;
	uint total_gc_reads = 0;
	uint total_gc_scheduled = 0;
	uint total_copy_backs = 0;
	uint total_erases = 0;
	double avg_overall_write_wait_time = 0;
	double avg_overall_read_wait_time = 0;
	double avg_overall_gc_wait_time = 0;

	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			total_writes += num_writes_per_LUN[i][j];
			total_reads += num_reads_per_LUN[i][j];
			total_gc_writes += num_gc_writes_per_LUN_origin[i][j];
			total_gc_reads += num_gc_reads_per_LUN[i][j];
			total_gc_scheduled += num_gc_scheduled_per_LUN[i][j];
			total_copy_backs += num_copy_backs_per_LUN[i][j];
			total_erases += num_erases_per_LUN[i][j];

			double average_write_wait_time = sum_bus_wait_time_for_writes_per_LUN[i][j] / num_writes_per_LUN[i][j];

			avg_overall_write_wait_time += average_write_wait_time;

			double average_read_wait_time = sum_bus_wait_time_for_reads_per_LUN[i][j] / num_reads_per_LUN[i][j];

			avg_overall_read_wait_time += average_read_wait_time;

			if (num_writes_per_LUN[i][j] == 0) {
				average_write_wait_time = 0;
			}
			if (num_reads_per_LUN[i][j] == 0) {
				average_read_wait_time = 0;
			}
			double avg_age = compute_average_age(i, j);

			printf("C%d D%d\t", i, j);

			printf("%d\t\t", num_writes_per_LUN[i][j]);
			printf("%d\t\t", num_reads_per_LUN[i][j]);

			printf("%d\t\t", num_gc_writes_per_LUN_origin[i][j]);
			printf("%d\t\t", num_gc_reads_per_LUN[i][j]);
			printf("%d\t\t", num_copy_backs_per_LUN[i][j]);


			printf("%d\t\t", num_erases_per_LUN[i][j]);

			printf("%f\t", average_write_wait_time);
			printf("%f\t", average_read_wait_time);

			//printf("%f\t", avg_age);
			printf("\n");
		}
	}
	avg_overall_write_wait_time /= SSD_SIZE * PACKAGE_SIZE;
	avg_overall_read_wait_time /= SSD_SIZE * PACKAGE_SIZE;
	printf("\nTotals:\t");
	printf("%d\t\t", total_writes);
	printf("%d\t\t", total_reads);
	printf("%d\t\t", total_gc_writes);
	printf("%d\t\t", total_gc_reads);
	printf("%d\t\t", total_copy_backs);
	printf("%d\t\t", total_erases);
	printf("%f\t\t", avg_overall_write_wait_time);
	printf("%f\t\t", avg_overall_read_wait_time);
	printf("\n\n");
}

void StatisticsGatherer::print_gc_info() {
	printf("\n\t");
	printf("GC writes from\t");
	printf("GC writes to\t");
	printf("GC reads\t");
	printf("GC scheduled\t");
	printf("GC exec\t\t");
	printf("GC wait \t\t");
	printf("copy backs\t");
	printf("erases\t\t");
	//printf("age\t");

	printf("\n");

	uint total_gc_writes_origin = 0;
	uint total_gc_writes_destination = 0;
	uint total_gc_reads = 0;
	uint total_gc_scheduled = 0;
	uint total_gc_exec = 0;
	uint total_copy_backs = 0;
	uint total_erases = 0;
	double avg_overall_gc_wait_time = 0;

	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			total_gc_writes_origin += num_gc_writes_per_LUN_origin[i][j];
			total_gc_writes_destination += num_gc_writes_per_LUN_destination[i][j];
			total_gc_reads += num_gc_reads_per_LUN[i][j];
			total_gc_scheduled += num_gc_scheduled_per_LUN[i][j];
			total_gc_exec += num_executed_gc_ops[i][j];
			total_copy_backs += num_copy_backs_per_LUN[i][j];
			total_erases += num_erases_per_LUN[i][j];

			double average_gc_wait_per_LUN = sum_gc_wait_time_per_LUN[i][j] / num_gc_writes_per_LUN_destination[i][j];

			double average_migrations_per_gc = (double) num_live_pages_in_gc_exec[i][j] / num_executed_gc_ops[i][j];

			avg_overall_gc_wait_time += average_gc_wait_per_LUN;

			double avg_age = compute_average_age(i, j);

			printf("C%d D%d\t", i, j);

			printf("%d\t\t", num_gc_writes_per_LUN_origin[i][j]);
			printf("%d\t\t", num_gc_writes_per_LUN_destination[i][j]);
			printf("%d\t\t", num_gc_reads_per_LUN[i][j]);
			printf("%d\t\t", num_gc_scheduled_per_LUN[i][j]);
			printf("%d\t\t", num_executed_gc_ops[i][j]);
			printf("%f\t\t", average_gc_wait_per_LUN);
			printf("%d\t\t", num_copy_backs_per_LUN[i][j]);
			printf("%d\t\t", num_erases_per_LUN[i][j]);
			printf("%f\t\t", average_migrations_per_gc);
			//printf("%f\t", average_write_wait_time);
			//printf("%f\t", average_read_wait_time);

			//printf("%f\t", avg_age);
			printf("\n");
		}
	}
	avg_overall_gc_wait_time /= SSD_SIZE * PACKAGE_SIZE;
	printf("\nTotals:\t");
	printf("%d\t\t", total_gc_writes_origin);
	printf("%d\t\t", total_gc_writes_destination);
	printf("%d\t\t", total_gc_reads);
	printf("%d\t\t", total_gc_scheduled);
	printf("%d\t\t", total_gc_exec);
	printf("%f\t\t", avg_overall_gc_wait_time);
	printf("%d\t\t", total_copy_backs);
	printf("%d\t\t", total_erases);
	printf("\n\n");
	printf("num scheduled gc: %d \n", num_gc_scheduled);
	printf("num executed gc: %d \n", num_gc_executed);
	printf("num migrations per gc: %f \n", (double)num_migrations / num_gc_executed);
	printf("\n");
	printf("gc targeting package die class: %d \n", num_gc_targeting_package_die_class);
	printf("gc targeting package die: %d \n", num_gc_targeting_package_die);
	printf("gc targeting package class: %d \n", num_gc_targeting_package_class);
	printf("gc targeting package: %d \n", num_gc_targeting_package);
	printf("gc targeting class: %d \n", num_gc_targeting_class);
	printf("gc targeting anything: %d \n", num_gc_targeting_anything);
	printf("\n");
	printf("num_gc_cancelled_no_candidate: %d \n", num_gc_cancelled_no_candidate);
	printf("num_gc_cancelled_not_enough_free_space: %d \n", num_gc_cancelled_not_enough_free_space);
	printf("num_gc_cancelled_gc_already_happening: %d \n", num_gc_cancelled_gc_already_happening);
}



string StatisticsGatherer::totals_csv_header() {
	stringstream ss;
	string q = "\"";
	string qc = "\", ";
	ss << q << "num writes" << qc;
	ss << q << "num reads" << qc;
	ss << q << "GC write" << qc;
	ss << q << "GC reads" << qc;
	ss << q << "GC scheduled" << qc;
	ss << q << "GC wait" << qc;
	ss << q << "copy backs" << qc;
	ss << q << "erases" << qc;
	ss << q << "avg write wait (µs)" << qc;
	ss << q << "avg read wait (µs)" << q;
//	ss << "\n";
	return ss.str();
}

string StatisticsGatherer::totals_csv_line() {
	uint total_writes = 0;
	uint total_reads = 0;
	uint total_gc_writes = 0;
	uint total_gc_reads = 0;
	uint total_gc_scheduled = 0;
	uint total_copy_backs = 0;
	uint total_erases = 0;
	double avg_overall_write_wait_time = 0;
	double avg_overall_read_wait_time = 0;
	double avg_overall_gc_wait_time = 0;

	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			total_writes += num_writes_per_LUN[i][j];
			total_reads += num_reads_per_LUN[i][j];
			total_gc_writes += num_gc_writes_per_LUN_origin[i][j];
			total_gc_reads += num_gc_reads_per_LUN[i][j];
			total_gc_scheduled += num_gc_scheduled_per_LUN[i][j];
			total_copy_backs += num_copy_backs_per_LUN[i][j];
			total_erases += num_erases_per_LUN[i][j];

			double average_write_wait_time = sum_bus_wait_time_for_writes_per_LUN[i][j] / num_writes_per_LUN[i][j];

			avg_overall_write_wait_time += average_write_wait_time;

			double average_read_wait_time = sum_bus_wait_time_for_reads_per_LUN[i][j] / num_reads_per_LUN[i][j];

			avg_overall_read_wait_time += average_read_wait_time;

			double average_gc_wait_per_LUN = sum_gc_wait_time_per_LUN[i][j] / num_gc_writes_per_LUN_origin[i][j];

			avg_overall_gc_wait_time += average_gc_wait_per_LUN;

			if (num_writes_per_LUN[i][j] == 0) {
				average_write_wait_time = 0;
			}
			if (num_reads_per_LUN[i][j] == 0) {
				average_read_wait_time = 0;
			}
			double avg_age = compute_average_age(i, j);
		}
	}
	avg_overall_write_wait_time /= SSD_SIZE * PACKAGE_SIZE;
	avg_overall_read_wait_time /= SSD_SIZE * PACKAGE_SIZE;
	avg_overall_gc_wait_time /= SSD_SIZE * PACKAGE_SIZE;
	stringstream ss;
	ss << total_writes << ", ";
	ss << total_reads << ", ";
	ss << total_gc_writes << ", ";
	ss << total_gc_reads << ", ";
	ss << total_gc_scheduled << ", ";
	ss << avg_overall_write_wait_time << ", ";
	ss << total_copy_backs << ", ";
	ss << total_erases << ", ";
	ss << avg_overall_write_wait_time << ", ";
	ss << avg_overall_read_wait_time;
//	ss << "\n";
	return ss.str();
}

string StatisticsGatherer::histogram_csv(map<double, uint> histogram) {
	stringstream ss;
	ss << "\"Interval\", \"Frequency\"" << "\n";
	for (map<double,uint>::iterator it = histogram.begin(); it != histogram.end(); ++it) {
		ss << it->first << ", " << it->second << "\n";
	}
	return ss.str();
}

uint StatisticsGatherer::max_age() {
	uint max_age = 0;
	map<double, uint> age_histogram;
	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			for (uint k = 0; k < DIE_SIZE; k++) {
				for (uint t = 0; t < PLANE_SIZE; t++) {
					Block const& block = ssd.getPackages()[i].getDies()[j].getPlanes()[k].getBlocks()[t];
					uint age = BLOCK_ERASES - block.get_erases_remaining();
					max_age = max(age, max_age);
				}
			}
		}
	}
	return max_age;
}

string StatisticsGatherer::age_histogram_csv() {
	map<double, uint> age_histogram;
	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			for (uint k = 0; k < DIE_SIZE; k++) {
				for (uint t = 0; t < PLANE_SIZE; t++) {
					Block const& block = ssd.getPackages()[i].getDies()[j].getPlanes()[k].getBlocks()[t];
					uint age = BLOCK_ERASES - block.get_erases_remaining();
					age_histogram[floor((double) age / age_histogram_steps)*age_histogram_steps]++;
				}
			}
		}
	}
	return histogram_csv(age_histogram);
}

string StatisticsGatherer::wait_time_histogram_csv() {
	return histogram_csv(wait_time_histogram);
}

string StatisticsGatherer::queue_length_csv() {
	stringstream ss;
	ss << "\"Time (µs)\", \"Queued events\"" << "\n";
	for (uint window = 0; window < queue_length_tracker.size(); window++) {
		ss << window*queue_length_tracker_resolution << ", " << queue_length_tracker[window] << "\n";
	}
	return ss.str();
}

void StatisticsGatherer::print_csv() {
	printf("\n");
	printf("Channel,");
	printf("Die,");
	printf("num writes,");
	printf("num reads,");
	printf("GC writes,");
	printf("GC reads,");
	printf("GC scheduled,");
	printf("copy backs,");
	printf("erases,");
	printf("write wait,");
	printf("read wait,");
	//printf("age\t");

	printf("\n");
	for (uint i = 0; i < SSD_SIZE; i++) {
		for (uint j = 0; j < PACKAGE_SIZE; j++) {
			double average_write_wait_time = sum_bus_wait_time_for_writes_per_LUN[i][j] / num_writes_per_LUN[i][j];
			double average_read_wait_time = sum_bus_wait_time_for_reads_per_LUN[i][j] / num_reads_per_LUN[i][j];
			if (num_writes_per_LUN[i][j] == 0) {
				average_write_wait_time = 0;
			}
			if (num_reads_per_LUN[i][j] == 0) {
				average_read_wait_time = 0;
			}
			double avg_age = compute_average_age(i, j);

			printf("%d,%d,", i, j);

			printf("%d,", num_writes_per_LUN[i][j]);
			printf("%d,", num_reads_per_LUN[i][j]);

			printf("%d,", num_gc_writes_per_LUN_origin[i][j]);
			printf("%d,", num_gc_reads_per_LUN[i][j]);
			printf("%d,", num_gc_scheduled_per_LUN[i][j]);
			printf("%d,", num_copy_backs_per_LUN[i][j]);
			printf("%d,", num_erases_per_LUN[i][j]);

			printf("%f,", average_write_wait_time);
			printf("%f,", average_read_wait_time);

			//printf("%f\t", avg_age);
			printf("\n");
		}
	}
}

double StatisticsGatherer::compute_average_age(uint package_id, uint die_id) {
	double sum_age = 0;
	for (uint i = 0; i < DIE_SIZE; i++) {
		for (uint j = 0; j < PLANE_SIZE; j++) {
			 Block& b = ssd.getPackages()[package_id].getDies()[die_id].getPlanes()[i].getBlocks()[j];
			 uint age = BLOCK_ERASES - b.get_erases_remaining();
			 sum_age += age;
		}
	}
	return sum_age / (DIE_SIZE * PLANE_SIZE);
}




