#include <iostream>
#include <assert.h>
#include <getopt.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sched.h>
#include <inttypes.h>
#include <sys/time.h>
#include <unistd.h>
#include <malloc.h>
#include <stdint.h>

//#include "getticks.h"
#include "common.h"
#include "utils.h"
#include "sspfd.h"
#include "ssmem.h"
#include "ssalloc.h"
#include "rapl_read.h"

#include "../framework/cpp_framework.h"
#include "../data_structures/HopscotchHashMap.h"

#ifdef __sparc__
#  include <sys/types.h>
#  include <sys/processor.h>
#  include <sys/procset.h>
#endif

using namespace CMDR;
using namespace std;

/* ################################################################### *
 * Definition of macros: per data structure
 * ################################################################### */

//! A concurrent hash table that maps ints to ints
typedef HopscotchHashMap<int, int, HASH_INT, TTASLock, CMDR::Memory> IntTable;
IntTable mset;

#define DS_CONTAINS(s,k)    s.containsKey(k);
#define DS_ADD(s,a,k)       s.putIfAbsent(a, k)
#define DS_REMOVE(s,k)      s.remove(k)
#define DS_SIZE(s)          s.size()
//#define DS_NEW()            ;

#define DS_TYPE             void*
#define DS_NODE             void*
#define HASWELL				1

/* ################################################################### *
 * GLOBALS
 * ################################################################### */

unsigned int maxhtlength;
size_t initial = DEFAULT_INITIAL;
size_t range = DEFAULT_RANGE;
size_t load_factor = 1;
size_t update = DEFAULT_UPDATE;
size_t num_threads = DEFAULT_NB_THREADS;
size_t duration = DEFAULT_DURATION;

size_t print_vals_num = 100;
size_t pf_vals_num = 1023;
size_t put, put_explicit = false;
double update_rate, put_rate, get_rate;

size_t size_after = 0;
int seed = 0;
__thread unsigned long * seeds;
uint32_t rand_max;
#define rand_min 1

static volatile int stop;

volatile ticks *putting_succ;
volatile ticks *putting_fail;
volatile ticks *getting_succ;
volatile ticks *getting_fail;
volatile ticks *removing_succ;
volatile ticks *removing_fail;
volatile ticks *putting_count;
volatile ticks *putting_count_succ;
volatile ticks *getting_count;
volatile ticks *getting_count_succ;
volatile ticks *removing_count;
volatile ticks *removing_count_succ;
volatile ticks *total;

/* ################################################################### *
 * LOCALS
 * ################################################################### */

#ifdef DEBUG
extern __thread uint32_t put_num_restarts;
extern __thread uint32_t put_num_failed_expand;
extern __thread uint32_t put_num_failed_on_new;
#endif

barrier_t barrier, barrier_global;

static uint8_t the_cores[] = { 0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 2, 3, 4, 5, 6, 7,

};

typedef struct thread_data {
	uint8_t id;
//	  unsigned int id;
} thread_data_t;

static AtomicInteger _threadCounter(0);
int* _gRandNumAry;
int _gTotalRandNum;
//AtomicInteger			_gThreadStartCounter(0);
//VolatileType<tick_t>	_gStartTime;
//VolatileType<tick_t>	_gEndTime;

void PrepareRandomNumbers() {
	HopscotchHashMap<int, int, HASH_INT, TTASLock, CMDR::Memory> num_map(_gTotalRandNum*2, 1);

	_gRandNumAry = new int[_gTotalRandNum];
	int last_number = 1;
	for (int iRandNum = 0; iRandNum < _gTotalRandNum; ++iRandNum) {
		//last_number += (_gRand.nextInt(256) + 1);
		//last_number = _gRand.nextInt(256*1024*1024) + 1;
		last_number += 1;
		_gRandNumAry[iRandNum] = last_number;
	}
}

void FillTable(int table_size) {
	for (int iRandNum = 0; iRandNum < table_size; ++iRandNum) {
		DS_ADD(mset, _gRandNumAry[iRandNum], _gRandNumAry[iRandNum]);
//		_gTestDs->put(_gRandNumAry[iRandNum], _gRandNumAry[iRandNum]);
		//cerr << _gRandNumAry[iRandNum] << endl;
	}
//	cerr << (string("    ") + _gTestDs->name() + " Num elm: " + Integer::toString(_gTestDs->size()))<< endl;
}

void*
test(void* thread) {
	thread_data_t* td = (thread_data_t*) thread;
	uint8_t ID = td->id;
//  unsigned int ID=td->id;
//  cout<<ID<<"##################"<<endl;
	int phys_id = the_cores[ID];
	cout << phys_id << "&&&&&&&&&&&&&" << endl;
//  int phys_id=0;
	set_cpu(phys_id);
	ssalloc_init();

	PF_INIT(3, SSPFD_NUM_ENTRIES, ID);

#if defined(COMPUTE_LATENCY)
	volatile ticks my_putting_succ = 0;
	volatile ticks my_putting_fail = 0;
	volatile ticks my_getting_succ = 0;
	volatile ticks my_getting_fail = 0;
	volatile ticks my_removing_succ = 0;
	volatile ticks my_removing_fail = 0;
#endif
	uint64_t my_putting_count = 0;
	uint64_t my_getting_count = 0;
	uint64_t my_removing_count = 0;

	uint64_t my_putting_count_succ = 0;
	uint64_t my_getting_count_succ = 0;
	uint64_t my_removing_count_succ = 0;

#if defined(COMPUTE_LATENCY) && PFD_TYPE == 0
	volatile ticks start_acq, end_acq;
	volatile ticks correction = getticks_correction_calc();
#endif

	seeds = seed_rand();
#if GC == 1
#endif

	RR_INIT(phys_id);
//	uint64_t key;
//
//	uint32_t c = 0;
//	uint32_t scale_rem = (uint32_t) (update_rate * UINT_MAX);
//	uint32_t scale_put = (uint32_t) (put_rate * UINT_MAX);

//	unsigned int i;
//	uint32_t num_elems_thread = (uint32_t) (initial / num_threads);
//	int32_t missing = (uint32_t) initial - (num_elems_thread * num_threads);
//	if (ID < missing) {
//		num_elems_thread++;
//	}
//
//#if INITIALIZE_FROM_ONE == 1
//	num_elems_thread = (ID == 0) * initial;
//#endif
//
//	for (i = 0; i < num_elems_thread; i++) {
//		key =
//				(my_random(&(seeds[0]), &(seeds[1]), &(seeds[2]))
//						% (rand_max + 1)) + rand_min;
//
////      IntTable::accessor a;
//		if (DS_ADD(mset, key, key) == false) {
//			i--;
//		}
//	}MEM_BARRIER;
//
//	barrier_cross(&barrier);
//
//	if (!ID) {
//		printf("#BEFORE size is: %zu\n", (size_t) DS_SIZE(mset));
//	}

	//****start*******
		int state;
		int state2;
		int curr_counter;

		int _threadNo;
		int _i_start_suc_rand;
		int _i_end_suc_rand;
		int _i_start_unsuc_rand;
		int _i_end_unsuc_rand;
		int _num_add;
		int _num_remove;
		int _num_contain;

		_u64 volatile seed = Random::getSeed();
		_threadNo = _threadCounter.getAndIncrement();
		int num_actions = (int) ((initial) / num_threads);

		_i_start_suc_rand = _threadNo * num_actions;
		_i_end_suc_rand = _i_start_suc_rand + num_actions;

		_i_start_unsuc_rand = _i_start_suc_rand + maxhtlength;
		_i_end_unsuc_rand = _i_end_suc_rand + maxhtlength;

		_num_add = ((update) / 2);
		_num_remove = ((update) / 2);
		_num_contain = 100 - _num_add - _num_remove;

		state = (Random::getRandom(seed, 1023)) & 1;
		state2 = (Random::getRandom(seed, 1023)) & 1;
		curr_counter = _num_add;
		if (1 == state2)
			curr_counter = _num_contain;

		unsigned int action_counter = 0;
		int i_suc = _i_start_unsuc_rand;
		int i_unsuc = _i_start_unsuc_rand;

		//**********end*******************8

	barrier_cross(&barrier_global);

	RR_START_SIMPLE();
	cout<<"$$$   "<<_threadNo<<endl;
	while (stop == 0) {
		cout<<state<<"\t\t"<<state2<<endl;
		if (0 == state) {
			if (0 == state2) {
				if (curr_counter > 0) {
					int res;
					int removed;
//	  						_gTestDs->remove(_gRandNumAry[i_suc]);
//	  						_gTestDs->put(_gRandNumAry[i_unsuc], _gRandNumAry[i_unsuc]);
//					START_TS(2);
					removed = DS_REMOVE(mset, _gRandNumAry[i_suc]);
//					if (removed) {
//						END_TS(2, my_removing_count_succ);ADD_DUR(my_removing_succ);
//						my_removing_count_succ++;
//					}END_TS_ELSE(5, my_removing_count - my_removing_count_succ,
//							my_removing_fail);
					my_removing_count++;

//					START_TS(1);
					res = DS_ADD(mset, _gRandNumAry[i_unsuc],
							_gRandNumAry[i_unsuc]);
//					if (res) {
//						//	      a->second = key;
//						END_TS(1, my_putting_count_succ);ADD_DUR(my_putting_succ);
//						my_putting_count_succ++;
//					}END_TS_ELSE(4, my_putting_count - my_putting_count_succ,
//							my_putting_fail);
//	  						action_counter+=2;
					my_putting_count++;

					++i_suc;
					++i_unsuc;
					if (i_suc >= _i_end_suc_rand) {
						state = 1;
						i_suc = _i_start_suc_rand;
						i_unsuc = _i_start_unsuc_rand;
					}
				}

				--curr_counter;
				if (curr_counter <= 0) {
					state2 = 1;
					curr_counter = _num_contain;
				}
			} else {
//	  					bool b1,b2;
//	  					b1=_gTestDs->containsKey(_gRandNumAry[i_suc]);
//	  					b2=_gTestDs->containsKey(_gRandNumAry[i_unsuc]);
				int res;
//				START_TS(0);
				res = DS_CONTAINS(mset, _gRandNumAry[i_suc])
//				;END_TS(0, my_getting_count);
//				if (res != 0) {
//					END_TS(0, my_getting_count_succ);ADD_DUR(my_getting_succ);
//					my_getting_count_succ++;
//				}END_TS_ELSE(3, my_getting_count - my_getting_count_succ,
//						my_getting_fail);
				my_getting_count++;
//
//				START_TS(0);
				res = DS_CONTAINS(mset, _gRandNumAry[i_unsuc])
				;
//	  					action_counter+=2;
//				END_TS(0, my_getting_count);
//				if (res != 0) {
//					END_TS(0, my_getting_count_succ);ADD_DUR(my_getting_succ);
//					my_getting_count_succ++;
//				}END_TS_ELSE(3, my_getting_count - my_getting_count_succ,
//						my_getting_fail);
				my_getting_count++;

				++i_suc;
				++i_unsuc;
				if (i_suc >= _i_end_suc_rand) {
					state = 1;
					i_suc = _i_start_suc_rand;
					i_unsuc = _i_start_unsuc_rand;
				}

				--curr_counter;
				if (curr_counter <= 0) {
					state2 = 0;
					curr_counter = _num_add;
				}
			}
		} else if (1 == state) {
			if (0 == state2) {
				if (curr_counter > 0) {
//	  						_gTestDs->put(_gRandNumAry[i_suc], _gRandNumAry[i_suc]);
//	  						_gTestDs->remove(_gRandNumAry[i_unsuc]);
					int res;
					int removed;
//					DS_ADD(mset, _gRandNumAry[i_suc], _gRandNumAry[i_suc]);
//					DS_REMOVE(mset, _gRandNumAry[i_unsuc]);
//	  						action_counter+=2;
//					START_TS(1);
					res = DS_ADD(mset, _gRandNumAry[i_suc],
							_gRandNumAry[i_suc]);
//					if (res) {
//						//	      a->second = key;
//						END_TS(1, my_putting_count_succ);ADD_DUR(my_putting_succ);
//						my_putting_count_succ++;
//					}END_TS_ELSE(4, my_putting_count - my_putting_count_succ,
//							my_putting_fail);
					//	  						action_counter+=2;
					my_putting_count++;

					//	  						_gTestDs->remove(_gRandNumAry[i_suc]);
//					//	  						_gTestDs->put(_gRandNumAry[i_unsuc], _gRandNumAry[i_unsuc]);
//					START_TS(2);
					removed = DS_REMOVE(mset, _gRandNumAry[i_unsuc]);
//					if (removed) {
//						END_TS(2, my_removing_count_succ);ADD_DUR(my_removing_succ);
//						my_removing_count_succ++;
//					}END_TS_ELSE(5, my_removing_count - my_removing_count_succ,
//							my_removing_fail);
					my_removing_count++;

					++i_suc;
					++i_unsuc;
					if (i_suc >= _i_end_suc_rand) {
						state = 0;
						i_suc = _i_start_suc_rand;
						i_unsuc = _i_start_unsuc_rand;
					}
				}

				--curr_counter;
				if (curr_counter <= 0) {
					state2 = 1;
					curr_counter = _num_contain;
				}
			} else {
//	  					bool b1,b2;
//	  					b1=_gTestDs->containsKey(_gRandNumAry[i_suc]);
//	  					b2=_gTestDs->containsKey(_gRandNumAry[i_unsuc]);
//	  					action_counter+=2;
				int res;
//				START_TS(0);
				res = DS_CONTAINS(mset, _gRandNumAry[i_suc]);
//				END_TS(0, my_getting_count);
//				if (res != 0) {
//					END_TS(0, my_getting_count_succ);ADD_DUR(my_getting_succ);
//					my_getting_count_succ++;
//				}END_TS_ELSE(3, my_getting_count - my_getting_count_succ,
//						my_getting_fail);
				my_getting_count++;

//				START_TS(0);
				res = DS_CONTAINS(mset, _gRandNumAry[i_unsuc])
				;
				//	  					action_counter+=2;
//				END_TS(0, my_getting_count);
//				if (res != 0) {
//					END_TS(0, my_getting_count_succ);ADD_DUR(my_getting_succ);
//					my_getting_count_succ++;
//				}END_TS_ELSE(3, my_getting_count - my_getting_count_succ,
//						my_getting_fail);
				my_getting_count++;

				++i_suc;
				++i_unsuc;
				if (i_suc >= _i_end_suc_rand) {
					state = 0;
					i_suc = _i_start_suc_rand;
					i_unsuc = _i_start_unsuc_rand;
				}

				--curr_counter;
				if (curr_counter <= 0) {
					state2 = 0;
					curr_counter = _num_add;
				}
			}
		}
	}
//	cout << "test-=start****=" <<stop<< endl;
	barrier_cross(&barrier);
	RR_STOP_SIMPLE();

	if (!ID) {
		size_after = DS_SIZE(mset);
		printf("#AFTER  size is: %zu\n", size_after);
	}

	barrier_cross(&barrier);

#if defined(COMPUTE_LATENCY)
	putting_succ[ID] += my_putting_succ;
	putting_fail[ID] += my_putting_fail;
	getting_succ[ID] += my_getting_succ;
	getting_fail[ID] += my_getting_fail;
	removing_succ[ID] += my_removing_succ;
	removing_fail[ID] += my_removing_fail;
#endif
	putting_count[ID] += my_putting_count;
	getting_count[ID] += my_getting_count;
	removing_count[ID] += my_removing_count;

	putting_count_succ[ID] += my_putting_count_succ;
	getting_count_succ[ID] += my_getting_count_succ;
	removing_count_succ[ID] += my_removing_count_succ;

	EXEC_IN_DEC_ID_ORDER(ID, num_threads)
				{
					print_latency_stats(ID, SSPFD_NUM_ENTRIES, print_vals_num);
				}EXEC_IN_DEC_ID_ORDER_END(&barrier);

	SSPFDTERM();
#if GC == 1
#endif
//	cout << "test ****" << endl;
	pthread_exit(NULL);
}

int main(int argc, char **argv) {
//	unsigned int len=(unsigned int)sizeof(the_cores)/sizeof(&the_cores[0]);
//		cout<<len<<"&&&&&&&&&"<<endl;
//		for(unsigned int i=0;i<len;i++) {
//			cout<<the_cores[i]<<" %%%"<<endl;
//		}
//		cout<<endl;
	set_cpu(the_cores[0]);
//	set_cpu(0);
	ssalloc_init();
	seeds = seed_rand();

	struct option long_options[] = {
	// These options don't set a flag
			{ "help", no_argument, NULL, 'h' }, { "duration", required_argument,
			NULL, 'd' }, { "initial-size", required_argument, NULL, 'i' }, {
					"num-threads",
					required_argument, NULL, 'n' }, { "range",
			required_argument, NULL, 'r' }, { "update-rate",
			required_argument, NULL, 'u' }, { "num-buckets",
			required_argument, NULL, 'b' }, { "print-vals",
			required_argument, NULL, 'v' }, { "vals-pf",
			required_argument, NULL, 'f' }, { "load-factor",
			required_argument, NULL, 'l' }, { NULL, 0, NULL, 0 } };

	int i, c;
	while (1) {
		i = 0;
		c = getopt_long(argc, argv, "hAf:d:i:n:r:s:u:m:a:l:p:b:v:f:",
				long_options, &i);

		if (c == -1)
			break;

		if (c == 0 && long_options[i].flag == 0)
			c = long_options[i].val;

		switch (c) {
		case 0:
			/* Flag is automatically set */
			break;
		case 'h':
			printf(
					"intset -- STM stress test "
							"(linked list)\n"
							"\n"
							"Usage:\n"
							"  intset [options...]\n"
							"\n"
							"Options:\n"
							"  -h, --help\n"
							"        Print this message\n"
							"  -d, --duration <int>\n"
							"        Test duration in milliseconds\n"
							"  -i, --initial-size <int>\n"
							"        Number of elements to insert before test\n"
							"  -n, --num-threads <int>\n"
							"        Number of threads\n"
							"  -r, --range <int>\n"
							"        Range of integer values inserted in set\n"
							"  -u, --update-rate <int>\n"
							"        Percentage of update transactions\n"
							"  -l, --load-factor <int>\n"
							"        Elements per bucket\n"
							"  -p, --put-rate <int>\n"
							"        Percentage of put update transactions (should be less than percentage of updates)\n"
							"  -b, --num-buckets <int>\n"
							"        Number of initial buckets (stronger than -l)\n"
							"  -v, --print-vals <int>\n"
							"        When using detailed profiling, how many values to print.\n"
							"  -f, --val-pf <int>\n"
							"        When using detailed profiling, how many values to keep track of.\n");
			exit(0);
		case 'd':
			duration = atoi(optarg);
			break;
		case 'i':
			initial = atoi(optarg);
			cout<<"initial :="<<initial<<endl;
			break;
		case 'n':
			num_threads = atoi(optarg);
			break;
		case 'r':
			range = atol(optarg);
			break;
		case 'u':
			update = atoi(optarg);
			break;
		case 'p':
			put_explicit = 1;
			put = atoi(optarg);
			break;
		case 'l':
			load_factor = atoi(optarg);
			break;
			/* case 'b': */
			/*   num_buckets_param = atoi(optarg); */
			/*   break; */
		case 'v':
			print_vals_num = atoi(optarg);
			break;
		case 'f':
			pf_vals_num = pow2roundup(atoi(optarg)) - 1;
			break;
		case '?':
		default:
			printf("Use -h or --help for help\n");
			exit(1);
		}
	}

	if (!is_power_of_two(initial)) {
		size_t initial_pow2 = pow2roundup(initial);
		printf(
				"** rounding up initial (to make it power of 2): old: %zu / new: %zu\n",
				initial, initial_pow2);
		initial = initial_pow2;
	}

	if (range < initial) {
		range = 2 * initial;
	}

	printf("## Initial: %zu / Range: %zu / Load factor: %zu\n", initial, range,
			load_factor);

	double kb = initial * sizeof(DS_NODE) / 1024.0;
	double mb = kb / 1024.0;
	printf("Sizeof initial: %.2f KB = %.2f MB\n", kb, mb);

	if (!is_power_of_two(range)) {
		size_t range_pow2 = pow2roundup(range);
		printf(
				"** rounding up range (to make it power of 2): old: %zu / new: %zu\n",
				range, range_pow2);
		range = range_pow2;
	}

	if (put > update) {
		put = update;
	}

	update_rate = update / 100.0;

	if (put_explicit) {
		put_rate = put / 100.0;
	} else {
		put_rate = update_rate / 2;
	}

	get_rate = 1 - update_rate;

	/* printf("num_threads = %u\n", num_threads); */
	/* printf("cap: = %u\n", num_buckets); */
	/* printf("num elem = %u\n", num_elements); */
	/* printf("filing rate= %f\n", filling_rate); */
	/* printf("update = %f (putting = %f)\n", update_rate, put_rate); */

	rand_max = range - 1;

	struct timeval start, end;
	struct timespec timeout;
	timeout.tv_sec = duration / 1000;
	timeout.tv_nsec = (duration % 1000) * 1000000;

	stop = 0;

	maxhtlength = (unsigned int) initial / load_factor;
	cout<<"initial :="<<initial<<endl;
	_gTotalRandNum = 2 * maxhtlength;
	PrepareRandomNumbers();
	FillTable(initial);
	/* Initializes the local data */
	putting_succ = (ticks *) calloc(num_threads, sizeof(ticks));
	putting_fail = (ticks *) calloc(num_threads, sizeof(ticks));
	getting_succ = (ticks *) calloc(num_threads, sizeof(ticks));
	getting_fail = (ticks *) calloc(num_threads, sizeof(ticks));
	removing_succ = (ticks *) calloc(num_threads, sizeof(ticks));
	removing_fail = (ticks *) calloc(num_threads, sizeof(ticks));
	putting_count = (ticks *) calloc(num_threads, sizeof(ticks));
	putting_count_succ = (ticks *) calloc(num_threads, sizeof(ticks));
	getting_count = (ticks *) calloc(num_threads, sizeof(ticks));
	getting_count_succ = (ticks *) calloc(num_threads, sizeof(ticks));
	removing_count = (ticks *) calloc(num_threads, sizeof(ticks));
	removing_count_succ = (ticks *) calloc(num_threads, sizeof(ticks));
	seeds = (unsigned long*) calloc(3, sizeof(unsigned long));

	pthread_t threads[num_threads];
	pthread_attr_t attr;
	int rc;
	void *status;

	barrier_init(&barrier_global, num_threads + 1);
	barrier_init(&barrier, num_threads);

	/* Initialize and set thread detached attribute */
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

	thread_data_t* tds = (thread_data_t*) malloc(
			num_threads * sizeof(thread_data_t));

	unsigned int t;
	for (t = 0; t < num_threads; t++) {
		tds[t].id = t;
		rc = pthread_create(&threads[t], &attr, test, tds + t);
		if (rc) {
			printf("ERROR; return code from pthread_create() is %d\n", rc);
			exit(-1);
		}

	}

	/* Free attribute and wait for the other threads */
	pthread_attr_destroy(&attr);

	barrier_cross(&barrier_global);
	gettimeofday(&start, NULL);
	nanosleep(&timeout, NULL);

	stop = 1;
	gettimeofday(&end, NULL);
	duration = (end.tv_sec * 1000 + end.tv_usec / 1000)
			- (start.tv_sec * 1000 + start.tv_usec / 1000);
	cout << "****main" << endl;
	for (t = 0; t < num_threads; t++) {
//		cout << "join" << t << endl;
		rc = pthread_join(threads[t], &status);
		if (rc) {
			printf("ERROR; return code from pthread_join() is %d\n", rc);
			exit(-1);
		}
	}

	free(tds);

	volatile ticks putting_suc_total = 0;
	volatile ticks putting_fal_total = 0;
	volatile ticks getting_suc_total = 0;
	volatile ticks getting_fal_total = 0;
	volatile ticks removing_suc_total = 0;
	volatile ticks removing_fal_total = 0;
	volatile uint64_t putting_count_total = 0;
	volatile uint64_t putting_count_total_succ = 0;
	volatile uint64_t getting_count_total = 0;
	volatile uint64_t getting_count_total_succ = 0;
	volatile uint64_t removing_count_total = 0;
	volatile uint64_t removing_count_total_succ = 0;

	for (t = 0; t < num_threads; t++) {
		putting_suc_total += putting_succ[t];
		putting_fal_total += putting_fail[t];
		getting_suc_total += getting_succ[t];
		getting_fal_total += getting_fail[t];
		removing_suc_total += removing_succ[t];
		removing_fal_total += removing_fail[t];
		putting_count_total += putting_count[t];
		putting_count_total_succ += putting_count_succ[t];
		getting_count_total += getting_count[t];
		getting_count_total_succ += getting_count_succ[t];
		removing_count_total += removing_count[t];
		removing_count_total_succ += removing_count_succ[t];
	}
	cout << "****end" << endl;
#if defined(COMPUTE_LATENCY)
	printf("#thread srch_suc srch_fal insr_suc insr_fal remv_suc remv_fal   ## latency (in cycles) \n"); fflush(stdout);
	long unsigned get_suc = (getting_count_total_succ) ? getting_suc_total / getting_count_total_succ : 0;
	long unsigned get_fal = (getting_count_total - getting_count_total_succ) ? getting_fal_total / (getting_count_total - getting_count_total_succ) : 0;
	long unsigned put_suc = putting_count_total_succ ? putting_suc_total / putting_count_total_succ : 0;
	long unsigned put_fal = (putting_count_total - putting_count_total_succ) ? putting_fal_total / (putting_count_total - putting_count_total_succ) : 0;
	long unsigned rem_suc = removing_count_total_succ ? removing_suc_total / removing_count_total_succ : 0;
	long unsigned rem_fal = (removing_count_total - removing_count_total_succ) ? removing_fal_total / (removing_count_total - removing_count_total_succ) : 0;
	printf("%-7zu %-8lu %-8lu %-8lu %-8lu %-8lu %-8lu\n", num_threads, get_suc, get_fal, put_suc, put_fal, rem_suc, rem_fal);
#endif

#define LLU long long unsigned int

	int UNUSED pr = (int) (putting_count_total_succ - removing_count_total_succ);
	if (size_after != (initial + pr)) {
		printf("// WRONG size. %zu + %d != %zu\n", initial, pr, size_after);
		assert(size_after == (initial + pr));
	}

	printf("    : %-10s | %-10s | %-11s | %-11s | %s\n", "total", "success",
			"succ %", "total %", "effective %");
	uint64_t total = putting_count_total + getting_count_total
			+ removing_count_total;
	double putting_perc = 100.0
			* (1 - ((double) (total - putting_count_total) / total));
	double putting_perc_succ = (1
			- (double) (putting_count_total - putting_count_total_succ)
					/ putting_count_total) * 100;
	double getting_perc = 100.0
			* (1 - ((double) (total - getting_count_total) / total));
	double getting_perc_succ = (1
			- (double) (getting_count_total - getting_count_total_succ)
					/ getting_count_total) * 100;
	double removing_perc = 100.0
			* (1 - ((double) (total - removing_count_total) / total));
	double removing_perc_succ = (1
			- (double) (removing_count_total - removing_count_total_succ)
					/ removing_count_total) * 100;
	printf("srch: %-10llu | %-10llu | %10.1f%% | %10.1f%% | \n",
			(LLU) getting_count_total, (LLU) getting_count_total_succ,
			getting_perc_succ, getting_perc);
	printf("insr: %-10llu | %-10llu | %10.1f%% | %10.1f%% | %10.1f%%\n",
			(LLU) putting_count_total, (LLU) putting_count_total_succ,
			putting_perc_succ, putting_perc,
			(putting_perc * putting_perc_succ) / 100);
	printf("rems: %-10llu | %-10llu | %10.1f%% | %10.1f%% | %10.1f%%\n",
			(LLU) removing_count_total, (LLU) removing_count_total_succ,
			removing_perc_succ, removing_perc,
			(removing_perc * removing_perc_succ) / 100);

	double throughput = (putting_count_total + getting_count_total
			+ removing_count_total) * 1000.0 / duration;
	printf("#txs %zu\t(%-10.0f\n", num_threads, throughput);
	printf("#Mops %.3f\n", throughput / 1e6);

//  RR_PRINT_UNPROTECTED(RAPL_PRINT_POW);
	RR_PRINT_CORRECTED();

	pthread_exit(NULL);

	return 0;
}
