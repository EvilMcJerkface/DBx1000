#include "global.h"
#include "ycsb.h"
#include "tpcc.h"
#include "test.h"
#include "thread.h"
#include "mem_alloc.h"
#include "transport.h"
#include "sequencer.h"

//void * f(void *);
//void * g(void *);
//void * worker(void *);
void * nn_worker(void *);
void network_test();
void network_test_recv();


// TODO the following global variables are HACK
Seq_thread_t * m_thds;

// defined in parser.cpp
void parser(int argc, char * argv[]);

int main(int argc, char* argv[])
{
    printf("Running sequencer...\n\n");
	// 0. initialize global data structure
	parser(argc, argv);
	assert(CC_ALG == CALVIN);
    assert(g_node_id == g_node_cnt + g_client_node_cnt);
	//assert(g_txn_inflight > 

	uint64_t seed = get_sys_clock();
	srand(seed);
	printf("Random seed: %ld\n",seed);

#if NETWORK_TEST
	tport_man.init(g_node_id);
	sleep(3);
	if(g_node_id == 0)
		network_test();
	else if(g_node_id == 1)
		network_test_recv();

	return 0;
#endif

	int64_t starttime;
	int64_t endtime;
    starttime = get_server_clock();
	// per-partition malloc
	mem_allocator.init(g_part_cnt, MEM_SIZE / g_part_cnt); 
	stats.init();
	printf("mem_allocator initialized!\n");
	workload * m_wl;
	switch (WORKLOAD) {
		case YCSB :
			m_wl = new ycsb_wl; break;
		case TPCC :
			m_wl = new tpcc_wl; break;
		case TEST :
			m_wl = new TestWorkload; 
			((TestWorkload *)m_wl)->tick();
			break;
		default:
			assert(false);
	}
	m_wl->init();
	printf("workload initialized!\n");

	rem_qry_man.init(g_node_id,m_wl);
	tport_man.init(g_node_id);
	seq_man.init(m_wl);

	// 2. spawn multiple threads
	uint64_t thd_cnt = g_seq_thread_cnt;

	pthread_t * p_thds = 
		(pthread_t *) malloc(sizeof(pthread_t) * (thd_cnt));
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	cpu_set_t cpus;
	m_thds = new Seq_thread_t[thd_cnt];
	pthread_barrier_init( &warmup_bar, NULL, thd_cnt );
	for (uint32_t i = 0; i < thd_cnt; i++) 
		m_thds[i].init(i, g_node_id, m_wl);
  endtime = get_server_clock();
  printf("Initialization Time = %ld\n", endtime - starttime);
	warmup_finish = true;
	pthread_barrier_init( &warmup_bar, NULL, thd_cnt);
#ifndef NOGRAPHITE
	CarbonBarrierInit(&enable_barrier, thd_cnt);
#endif
	pthread_barrier_init( &warmup_bar, NULL, thd_cnt);

	uint64_t cpu_cnt = 0;
	// spawn and run txns again.
	starttime = get_server_clock();

	uint32_t numCPUs = sysconf(_SC_NPROCESSORS_ONLN);
	printf("num cpus: %u\n",numCPUs);
	for (uint32_t i = 0; i < thd_cnt; i++) {
		uint64_t vid = i;
		CPU_ZERO(&cpus);
#if TPORT_TYPE_IPC
        //CPU_SET(g_node_id * thd_cnt + cpu_cnt, &cpus);
#else
        CPU_SET(cpu_cnt, &cpus);
#endif
		cpu_cnt++;
        //pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpus);
		if (i == thd_cnt - 1)
			nn_worker((void *)(vid));
		else
			pthread_create(&p_thds[i], &attr, nn_worker, (void *)vid);
    }

    //nn_worker((void *)(thd_cnt - 1));

	for (uint32_t i = 0; i < thd_cnt; i++) 
		pthread_join(p_thds[i], NULL);

	endtime = get_server_clock();
	
	if (WORKLOAD != TEST) {
		printf("SEQUENCER PASS! SimTime = %ld\n", endtime - starttime);
		if (STATS_ENABLE)
			stats.print_sequencer(false);
	} else {
		((TestWorkload *)m_wl)->summarize();
	}
	return 0;
}

//void * worker(void * id) {
//	//uint64_t tid = (uint64_t)id;
//	//m_thds[tid].run();
//	return NULL;
//}

void * nn_worker(void * id) {
	uint64_t tid = (uint64_t)id;
	m_thds[tid].run_remote();
	return NULL;
}

void network_test() {

	ts_t start;
	ts_t end;
	double time;
	int bytes;
	for(int i=4; i < 257; i+=4) {
		time = 0;
		for(int j=0;j < 1000; j++) {
			start = get_sys_clock();
			tport_man.simple_send_msg(i);
			while((bytes = tport_man.simple_recv_msg()) == 0) {}
			end = get_sys_clock();
			assert(bytes == i);
			time += end-start;
		}
		time = time/1000;
		printf("Network Bytes: %d, s: %f\n",i,time/BILLION);
        fflush(stdout);
	}
}

void network_test_recv() {
	int bytes;
	while(1) {
		if( (bytes = tport_man.simple_recv_msg()) > 0)
			tport_man.simple_send_msg(bytes);
	}
}
