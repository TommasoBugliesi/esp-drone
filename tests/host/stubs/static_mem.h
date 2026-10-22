#define STATIC_MEM_TASK_ALLOC(NAME, DEPTH) static const int osSys_##NAME##Depth = (DEPTH);
#define STATIC_MEM_TASK_CREATE(NAME, FN, TN, P, PRI) ((void)osSys_##NAME##Depth, (void)(FN), (void)(TN), (void)(PRI))
#define STATIC_MEM_QUEUE_ALLOC(NAME, LEN, SZ) static uint8_t osSys_##NAME##Storage[(LEN)*(SZ)];
#define STATIC_MEM_QUEUE_CREATE(NAME) ((xQueueHandle)osSys_##NAME##Storage)
