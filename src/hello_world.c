#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <modbus.h>
#include <errno.h>
#include <libbacnet/address.h>
#include <libbacnet/device.h>
#include <libbacnet/handlers.h>
#include <libbacnet/datalink.h>
#include <libbacnet/bvlc.h>
#include <libbacnet/client.h>
#include <libbacnet/txbuf.h>
#include <libbacnet/tsm.h>
#include <libbacnet/ai.h>
#include "bacnet_namespace.h"




#define SERVER_ADDR 	"140.159.153.159"
#define SERVER_PORT 	502
#define DATA_LENGTH 	256
#define QUIT_STRING 	"exit" 

#define PREVIOUS_CODE   0
#define RUN_AS_KIM	1

#if RUN_AS_KIM

#define BACNET_INSTANCE_NO	    26
#define BACNET_PORT		    0xBAC1
#define BACNET_INTERFACE	    "lo"
#define BACNET_DATALINK_TYPE	    "bvlc"
#define BACNET_SELECT_TIMEOUT_MS    1	    /* ms */

#define RUN_AS_BBMD_CLIENT	    1

#if RUN_AS_BBMD_CLIENT
#define BACNET_BBMD_PORT	    0xBAC0
#define BACNET_BBMD_ADDRESS	    "127.0.0.1"
#define BACNET_BBMD_TTL		    90
#endif

/* If you are trying out the test suite from home, this data matches the data
* stored in RANDOM_DATA_POOL for device number 12
* BACnet client will print "Successful match" whenever it is able to receive
* this set of data. Note that you will not have access to the RANDOM_DATA_POOL
* for your final submitted application. */
uint16_t tab_reg[2]={};
static uint16_t test_data[] = {
	0xA4EC, 0x6E39, 0x8740, 0x1065, 0x9134, 0xFC8C };
#define NUM_TEST_DATA (sizeof(test_data)/sizeof(test_data[0]))

static pthread_mutex_t timer_lock = PTHREAD_MUTEX_INITIALIZER;
static int Update_Analog_Input_Read_Property(
			BACNET_READ_PROPERTY_DATA *rpdata) {

	static int index;

	int instance_no =bacnet_Analog_Input_Instance_To_Index (rpdata->object_instance);
	if (rpdata->object_property != bacnet_PROP_PRESENT_VALUE) goto not_pv;
	printf("AI_Present_Value request for instance %i\n", instance_no);
/* Update the values to be sent to the BACnet client here.
* The data should be read from the head of a linked list. You are required
* to implement this list functionality.
*
* bacnet_Analog_Input_Present_Value_Set()
* First argument: Instance No
* Second argument: data to be sent
*
* Without reconfiguring libbacnet, a maximum of 4 values may be sent */
	bacnet_Analog_Input_Present_Value_Set(0, tab_reg[0]);
	bacnet_Analog_Input_Present_Value_Set(1, tab_reg[1]);
	printf("MODBUS DATA0 IS %d\n",tab_reg[0]);
	printf("MODBUS DATA1 IS %d\n",tab_reg[1]);

/* bacnet_Analog_Input_Present_Value_Set(1, test_data[index++]); */
/* bacnet_Analog_Input_Present_Value_Set(2, test_data[index++]); */
	if (index == NUM_TEST_DATA) index = 0;
	not_pv:
	return bacnet_Analog_Input_Read_Property(rpdata);
}

static bacnet_object_functions_t server_objects[] = {
	{bacnet_OBJECT_DEVICE,
		NULL,
		bacnet_Device_Count,
		bacnet_Device_Index_To_Instance,
		bacnet_Device_Valid_Object_Instance_Number,
		bacnet_Device_Object_Name,
		bacnet_Device_Read_Property_Local,
		bacnet_Device_Write_Property_Local,
		bacnet_Device_Property_Lists,
		bacnet_DeviceGetRRInfo,
		NULL, /* Iterator */
		NULL, /* Value_Lists */
		NULL, /* COV */
		NULL, /* COV Clear */
		NULL /* Intrinsic Reporting */
	},
	{bacnet_OBJECT_ANALOG_INPUT,
		bacnet_Analog_Input_Init,
		bacnet_Analog_Input_Count,
		bacnet_Analog_Input_Index_To_Instance,
		bacnet_Analog_Input_Valid_Instance,
		bacnet_Analog_Input_Object_Name,
		Update_Analog_Input_Read_Property,
		bacnet_Analog_Input_Write_Property,
		bacnet_Analog_Input_Property_Lists,
		NULL /* ReadRangeInfo */ ,
		NULL /* Iterator */ ,
		bacnet_Analog_Input_Encode_Value_List,
		bacnet_Analog_Input_Change_Of_Value,
		bacnet_Analog_Input_Change_Of_Value_Clear,
		bacnet_Analog_Input_Intrinsic_Reporting},
	{MAX_BACNET_OBJECT_TYPE}
};
static void register_with_bbmd(void) {
#if RUN_AS_BBMD_CLIENT
	/* Thread safety: Shares data with datalink_send_pdu */
	bacnet_bvlc_register_with_bbmd(
		bacnet_bip_getaddrbyname(BACNET_BBMD_ADDRESS),
		htons(BACNET_BBMD_PORT),
		BACNET_BBMD_TTL);
#endif
}
static void *minute_tick(void *arg) {
	while (1) {
		pthread_mutex_lock(&timer_lock);
		/* Expire addresses once the TTL has expired */
		bacnet_address_cache_timer(60);
		/* Re-register with BBMD once BBMD TTL has expired */
		register_with_bbmd();
		/* Update addresses for notification class recipient list
		* Requred for INTRINSIC_REPORTING
		* bacnet_Notification_Class_find_recipient(); */
		/* Sleep for 1 minute */
		pthread_mutex_unlock(&timer_lock);
		sleep(60);
	}
	return arg;
}
static void *second_tick(void *arg) {
	while (1) {
		pthread_mutex_lock(&timer_lock);
		
		/* Invalidates stale BBMD foreign device table entries */
		bacnet_bvlc_maintenance_timer(1);
		
		/* Transaction state machine: Responsible for retransmissions and ack
		* checking for confirmed services */
		bacnet_tsm_timer_milliseconds(1000);
		
		/* Re-enables communications after DCC_Time_Duration_Seconds
		* Required for SERVICE_CONFIRMED_DEVICE_COMMUNICATION_CONTROL
		* bacnet_dcc_timer_seconds(1); */
		
		/* State machine for load control object
		* Required for OBJECT_LOAD_CONTROL
		* bacnet_Load_Control_State_Machine_Handler(); */
		
		/* Expires any COV subscribers that have finite lifetimes
		* Required for SERVICE_CONFIRMED_SUBSCRIBE_COV
		* bacnet_handler_cov_timer_seconds(1); */
		
		/* Monitor Trend Log uLogIntervals and fetch properties
		* Required for OBJECT_TRENDLOG  *//*
		* bacnet_trend_log_timer(1); */
		
		/* Run [Object_Type]_Intrinsic_Reporting() for all objects in device
		* Required for INTRINSIC_REPORTING
		* bacnet_Device_local_reporting(); */
		
		/* Sleep for 1 second */
		pthread_mutex_unlock(&timer_lock);
		sleep(1);
	}
	return arg;
}

static void ms_tick(void) {
	/* Updates change of value COV subscribers.
	* Required for SERVICE_CONFIRMED_SUBSCRIBE_COV
	* bacnet_handler_cov_task(); */
}
#define BN_UNC(service, handler) \
	bacnet_apdu_set_unconfirmed_handler( \
			SERVICE_UNCONFIRMED_##service, \
			bacnet_handler_##handler)
#define BN_CON(service, handler) \
	bacnet_apdu_set_confirmed_handler( \
			SERVICE_CONFIRMED_##service, \
			bacnet_handler_##handler)

int modbus (void){
	modbus_t *ctx;
//	uint16_t tab_reg[64];
	int rc;
	int i;

	ctx = modbus_new_tcp("127.0.0.1", 502);//home
//	ctx = modbus_new_tcp("140.159.153.159", 502);//uni
	if (modbus_connect(ctx) == -1) {
	fprintf(stderr, "Connection failed: %s\n", modbus_strerror(errno));
	modbus_free(ctx);
	return -1;
	}
	
	rc = modbus_read_registers(ctx, 26, 2, tab_reg);
	if (rc == -1) {
	fprintf(stderr, "%s\n", modbus_strerror(errno));																		                   return -1;
	}

	for (i=0; i < rc; i++) {
	      printf("reg[%d]=%d (0x%X)\n", i, tab_reg[i], tab_reg[i]);
																										           }
																											   modbus_close(ctx);			
																											   modbus_free(ctx);

}

int main(int argc, char **argv) {
uint8_t rx_buf[bacnet_MAX_MPDU];
uint16_t pdu_len;
BACNET_ADDRESS src;
pthread_t minute_tick_id, second_tick_id;
bacnet_Device_Set_Object_Instance_Number(BACNET_INSTANCE_NO);
bacnet_address_init();
/* Setup device objects */
bacnet_Device_Init(server_objects);
BN_UNC(WHO_IS, who_is);
BN_CON(READ_PROPERTY, read_property);
bacnet_BIP_Debug = true;
bacnet_bip_set_port(htons(BACNET_PORT));
bacnet_datalink_set(BACNET_DATALINK_TYPE);
bacnet_datalink_init(BACNET_INTERFACE);
atexit(bacnet_datalink_cleanup);
memset(&src, 0, sizeof(src));
register_with_bbmd();
bacnet_Send_I_Am(bacnet_Handler_Transmit_Buffer);
pthread_create(&minute_tick_id, 0, minute_tick, NULL);
pthread_create(&second_tick_id, 0, second_tick, NULL);

/* Start another thread here to retrieve your allocated registers from the
* modbus server. This thread should have the following structure (in a
* separate function):
*
* Initialise:
* Connect to the modbus server
*
* Loop:
* Read the required number of registers from the modbus server
* Store the register data into the tail of a linked list
*/

while (1) {
	pdu_len = bacnet_datalink_receive(
	&src, rx_buf, bacnet_MAX_MPDU, BACNET_SELECT_TIMEOUT_MS);
	if (pdu_len) {
	/* May call any registered handler.
	* Thread safety: May block, however we still need to guarantee
	* atomicity with the timers, so hold the lock anyway */
		pthread_mutex_lock(&timer_lock);
		bacnet_npdu_handler(&src, rx_buf, pdu_len);
		pthread_mutex_unlock(&timer_lock);
		modbus();
		}
	ms_tick();
	}	
return 0;
}  

    

#endif	

#if PREVIOUS_CODE

///////////////* Linked list object */

typedef struct s_word_object word_object;
struct s_word_object {
    char *word;
    word_object *next;
};

/* list_head: Shared between two threads, must be accessed with list_lock */
static word_object *list_head;
static pthread_mutex_t list_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t list_data_ready = PTHREAD_COND_INITIALIZER;
static pthread_cond_t list_data_flush = PTHREAD_COND_INITIALIZER;

/* Add object to list */
static void add_to_list(char *word) {
    word_object *last_object, *tmp_object;
    char *tmp_string;
    
    /* Do all memory allocation outside of locking - strdup() and malloc() can
     * block */
    tmp_object = malloc(sizeof(word_object));
    tmp_string = strdup(word);

    /* Set up tmp_object outside of locking */
    tmp_object->word = tmp_string;
    tmp_object->next = NULL;

    pthread_mutex_lock(&list_lock);

    if (list_head == NULL) {
	/* The list is empty, just place our tmp_object at the head */
	list_head = tmp_object;
    } else {
	/* Iterate through the linked list to find the last object */
	last_object = list_head;
	while (last_object->next) {
	    last_object = last_object->next;
	}
	/* Last object is now found, link in our tmp_object at the tail */
	last_object->next = tmp_object;
    }

    pthread_mutex_unlock(&list_lock);
    pthread_cond_signal(&list_data_ready);
}

/* Retrieve the first object in the linked list. Note that this function must
 * be called with list_lock held */
static word_object *list_get_first(void) {
    word_object *first_object;

    first_object = list_head;
    list_head = list_head->next;

    return first_object;
}

static void *print_func(void *arg) {
    word_object *current_object;

    fprintf(stderr, "Print thread starting\n");

    while(1) {
	pthread_mutex_lock(&list_lock);

	while (list_head == NULL) {
	    pthread_cond_wait(&list_data_ready, &list_lock);
	}

	current_object = list_get_first();

	pthread_mutex_unlock(&list_lock);

	/* printf() and free() can block, make sure that we've released
	 * list_lock first */
	printf("Print threads: %s\n", current_object->word);
	free(current_object->word);
	free(current_object);

	/* Let list_flush() know that we've done some work */
	pthread_cond_signal(&list_data_flush);
    }

    /* Silence compiler warning */
    return arg;
}

static void list_flush(void) {
    pthread_mutex_lock(&list_lock);

    while (list_head != NULL) {
	pthread_cond_signal(&list_data_ready);
	pthread_cond_wait(&list_data_flush, &list_lock);
    }

    pthread_mutex_unlock(&list_lock);
}

static void start_server(void) {
    int socket_fd;
    struct sockaddr_in server_address;
    struct sockaddr_in client_address;
    socklen_t client_address_len;
    int want_quit = 0;
    fd_set read_fds;
    int bytes;
    char data[DATA_LENGTH];
    pthread_t print_thread;

    fprintf(stderr, "Starting server\n");

    pthread_create(&print_thread, NULL, print_func, NULL);

    socket_fd = socket(AF_INET, SOCK_DGRAM, 0);

    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(SERVER_PORT);
    server_address.sin_addr.s_addr = INADDR_ANY;


    if (bind(socket_fd, (struct sockaddr *) &server_address, 
			    sizeof(server_address)) < 0) {
	fprintf(stderr, "Bind failed\n");
	exit(1);
    }
    
    FD_ZERO(&read_fds);
    FD_SET(socket_fd, &read_fds);
    while (!want_quit) {
	
	/* Wait until data has arrived */
	if (select(socket_fd + 1, &read_fds, NULL, NULL, NULL) < 0) {
	    fprintf(stderr, "Select failed\n");
	    exit(1);
	}

	if (!FD_ISSET(socket_fd, &read_fds)) continue;

	/* Read input data */
	bytes = recvfrom(socket_fd, data, sizeof(data), 0,
			(struct sockaddr *) &client_address, 
			&client_address_len);

	if (bytes < 0) {
	    fprintf(stderr, "Recvfrom failed\n");
	    exit(1);
	}

	/* Process data */
	if (!strcmp(data, QUIT_STRING)) want_quit = 1;
	else add_to_list(data);
    }
    
    list_flush();
}

static void start_client(int count) {
    int sock_fd;
    struct sockaddr_in addr;
    char input_word[DATA_LENGTH];

    fprintf(stderr, "Accepting %i input strings\n", count);

    if ((sock_fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
	fprintf(stderr, "Socket failed\n");
	exit(1);
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(SERVER_PORT);
    addr.sin_addr.s_addr = inet_addr(SERVER_ADDR);

    if (connect(sock_fd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
	fprintf(stderr, "Connect failed\n");
	exit(1);
    }

    while (scanf("%256s", input_word) != EOF) {
	if (send(sock_fd, input_word, strlen(input_word) + 1, 0) < 0) {
	    fprintf(stderr, "Send failed\n");
	    exit(1);
	}
	if (!--count) break;
    }
}

int modbus (void){
	modbus_t *ctx;
	//uint16_t tab_reg[64];
	int rc;
	int i;

	ctx = modbus_new_tcp("127.0.0.1", 502);//home
//	ctx = modbus_new_tcp("140.159.153.159", 502);//uni
	if (modbus_connect(ctx) == -1) {
	fprintf(stderr, "Connection failed: %s\n", modbus_strerror(errno));
	modbus_free(ctx);
	return -1;
	}
	
	rc = modbus_read_registers(ctx, 26, 2, tab_reg);
	if (rc == -1) {
	fprintf(stderr, "%s\n", modbus_strerror(errno));																		                   return -1;
	}

	for (i=0; i < rc; i++) {
	      printf("reg[%d]=%d (0x%X)\n", i, tab_reg[i], tab_reg[i]);
																										           }
																											   modbus_close(ctx);			
																											   modbus_free(ctx);

}
int main(int argc, char **argv) {
    int c;
    int option_index = 0;
    int count = -1;
    int server = 0;
    static struct option long_options[] = {
	{"count",   required_argument,	0, 'c'},
	{"server",  no_argument,	0, 's'},
	{0,         0,			0,  0 }
    };

    while (1) {
/*	c = getopt_long(argc, argv, "c:s", long_options, &option_index);
	if (c == -1)
	    break;

	switch (c) {
	    case 'c':
		count = atoi(optarg);
		break;
	    case 's':
		server = 1;
		break;*/
		modbus();
		sleep(1);

	}
   
/*
    if (server) start_server();
    else start_client(count);
*/
    return 0;
}
#endif
