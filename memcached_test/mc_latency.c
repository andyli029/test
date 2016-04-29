//install libmemcached && gcc -g mc_latency.c -lmemcached -lpthread

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/time.h>
#include <libmemcached/memcached.h>

#define NUM_KEYS 3

#define THREAD_MAX 1000

static pthread_t tids[THREAD_MAX];
static int cnts_ok[THREAD_MAX];
static int cnts_fail[THREAD_MAX];
struct timeval delays[THREAD_MAX];
static int start_all;
static int exit_all;

int test_sec = 10;
int threads = 500;
static int delay_us = 10*1000;

#define DEFAULT_MC_HOST "127.0.0.1"
char *mc_host = DEFAULT_MC_HOST;

char *keys[NUM_KEYS] = {
"key1",
"key2",
"key3",
};

int set_memc() {
	memcached_return_t err;
	memcached_server_st *server;
	memcached_st *memc;
	char value[10];
	int i;

	//paul
	//return 0;

	memc = memcached_create(NULL);
	server = memcached_server_list_append(NULL, mc_host, 12000, &err);
	err = memcached_server_push(memc, server);

	for (i = 0; i < NUM_KEYS; i++) {
		sprintf(value, "value-%d", i);
		err = memcached_set(memc, keys[i], strlen(keys[i]), value, strlen(value), (time_t) 0, 0);
		if (err != 0) {
			fprintf(stderr, "memcached set %s %s failed: %d.\n", keys[i], value, err);
			return err;
		}
	}

	memcached_free(memc);

	return 0;
}

void thread_handler(void *arg) {
	int idx = (int) (long) arg;
	memcached_return_t err;
	memcached_server_st *server;
	memcached_st *memc;
	char *val;
	size_t val_len;
	uint32_t flags;
	struct timeval start, end, res;


	memc = memcached_create(NULL);
	server = memcached_server_list_append(NULL, mc_host, 12000, &err);
	err = memcached_server_push(memc, server);
	
	//wait untail all threads are ready;
	while(!start_all) usleep(10000);

	//controlled by main thread.
	while(!exit_all) {
		gettimeofday(&start, NULL);
		val = memcached_get(memc, keys[idx%3], strlen(keys[idx%3]), &val_len, &flags, &err);
		gettimeofday(&end, NULL);
		timersub(&end, &start, &res);

		usleep(delay_us);

		if (err == 0) {
			timeradd(&res, &delays[idx], &delays[idx]);
			cnts_ok[idx]++;
		} else
			cnts_fail[idx]++;

	}
	//fprintf(stderr, "idx=%d, err=%d, val=%s\n", idx, err, val);
	free(val);
	memcached_free(memc);
}

void
main(int argc, char *argv[])
{
	int i, rv, num_all, opt;
	struct timeval res;
	unsigned long res_sec, res_msec;
	const char *opt_str = "c:t:d:s:h";

	opt = getopt(argc, argv, opt_str);
	while(opt != -1) {
		/*No sanity check for all! */
		switch(opt) {
		case 'c':
			threads= atoi(optarg);
			if (threads >= THREAD_MAX) {
				fprintf(stderr, "threads number is too large: %d >= %d\n", threads, THREAD_MAX);
				exit(1);
			}
			break;

		case 't':
			test_sec = atoi(optarg);
			break;

		case 'd':
			delay_us = atoi(optarg);
			break;

		case 's':
			mc_host = strdup(optarg);
			assert(mc_host != NULL);
			break;

		case 'h':
		case '?':
			fprintf(stderr, "-c: threads [500], -t: test seconds [10], -d: delay time(us) [10000], -s: host, -h: help\n");
			exit(1);

		default:
			/* Never reach here. */
			break;
		}

		opt = getopt(argc, argv, opt_str);
	}

	printf("thread number: %d\n", threads);
	printf("test time: %ds\n", test_sec);
	printf("delay time: %dus (for each get)\n", delay_us);	

	/* set them! */
	rv = set_memc();
	if (rv)
		exit(1);

	/* lauch all of the threads. */
	for (i = 0; i < threads; i++) {
		timerclear(&delays[i]);
		rv = pthread_create(&tids[i], NULL, (void *) thread_handler, i);
	}

	sleep(3);
	start_all = 1;

	/* stop the test after some time. */
	sleep(test_sec);
	exit_all = 1;

	for (i = 0; i < threads; i++)
		pthread_join(tids[i], NULL);

	/* get ok */
	num_all = 0;
	for (i = 0; i < threads; i++)
		num_all += cnts_ok[i];
	printf("GET OK: %.2f/s\n", num_all * 1.0 / test_sec);

	/* latency for ok cases. */
	timerclear(&res);
	for (i = 0; i < threads; i++)
		timeradd(&delays[i], &res, &res);
	res_sec = res.tv_sec;
	res_msec = res.tv_usec/1000;
	printf("latency: %.2fms [Only for good case]\n", (res_sec*1000 + res_msec)*1.0/num_all);

	/* get failure */
	num_all = 0;
	for (i = 0; i < threads; i++)
		num_all += cnts_fail[i];
	printf("GET FAIL: %.2f/s\n", num_all * 1.0 / test_sec);

}
