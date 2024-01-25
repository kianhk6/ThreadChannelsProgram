#include <ctype.h> // for isalnum
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#define MAX_FILES 100
#define MAX_PATH_LENGTH 256
#define ARRAY_SIZE 1000

int buffer_size;
int num_threads;
char *metadata_file_path;
int lock_config;
int global_checkpointing;
char *output_file_path;

int num_files;
pthread_t* threads;
int files_per_thread; //p value

pthread_mutex_t threads_finished_mutex = PTHREAD_MUTEX_INITIALIZER;

volatile int threads_finished = 0;

int modifiedArray[ARRAY_SIZE];
pthread_mutex_t elementLock[ARRAY_SIZE]; //for lockconfig = 2
pthread_mutex_t arrayAccessLock; //lockconfig = 1
int CASLock = 0; //0 - lock not taken, 1 - lock taken, lock config = 3

int final_index;

typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int count;
    int barrier_size;
} barrier_t;

void barrier_init(barrier_t *barrier, int size) {
    pthread_mutex_init(&barrier->mutex, NULL);
    pthread_cond_init(&barrier->cond, NULL);
    barrier->count = 0;
    barrier->barrier_size = size;
}

void barrier_wait(barrier_t *barrier) {
    pthread_mutex_lock(&barrier->mutex);
    
    barrier->count++;
    
    if (barrier->count == barrier->barrier_size) {
        barrier->count = 0;
        pthread_cond_broadcast(&barrier->cond);
    } else {
        while (pthread_cond_wait(&barrier->cond, &barrier->mutex) != 0);
    }
    
    pthread_mutex_unlock(&barrier->mutex);
}

int barrier_get_count(barrier_t *barrier) {
    pthread_mutex_lock(&barrier->mutex);
    int count = barrier->count;
    pthread_mutex_unlock(&barrier->mutex);
    return count;
}

barrier_t barrier;

struct FileEntry
{
    char path[MAX_PATH_LENGTH];
    float alpha;
    float beta;
};

struct CurrentIndex {
    int index;
    int isDone; // 0 if not done, 1 if done
};

struct FileEntry *files;

void writeOutput(){
    FILE *file = fopen (output_file_path, "w");
    if (file == NULL){
        printf ("Error opening output file\n");
        exit(EXIT_FAILURE);
    }
    if(global_checkpointing == 1){
        for (int i = 0; i < final_index - 1; i++){ //check if last index is supposed to be there
            fprintf(file, "%d\n", modifiedArray[i]);
        }
    }
    else{ 
        // the case where global checkpointing is 0
        // as this case is unperdicatbale we don't know how many indexes there are so we have to go through the whole array
        // since there is no predicability we would have to write bunch of 0 at the end
        for (int i = 0; i < ARRAY_SIZE; i++){ 
            fprintf(file, "%d\n", modifiedArray[i]);
        }
    }

    fclose(file);  //done writing
}

void lock_element (int index){
    pthread_mutex_lock(&elementLock[index]);
}

void unlock_element (int index){
    pthread_mutex_unlock(&elementLock[index]);
}

int compare_and_swap(int *lock, int expected, int given) {
    return __sync_bool_compare_and_swap(lock, expected, given);
}


// not sure if this is how we want to implement the amp value
float amplification_value(float beta, float sample_value)
{ // beta
    if (beta < 0)
    {
        exit(EXIT_FAILURE);
        return -1; // error
    }
    return beta * sample_value;
}

// previousvalue value can be set to NULL for the first value
float low_pass_filter_value(float alpha, float sample_value, float previous)
{   // alpha value calculation
    return alpha * sample_value + (1.0 - alpha) * previous;
}


// arguments: file address, current index of the file, buffer size (how much we
// are allowed to read in current iteration)
// output: file content from buffer to current_index, updates current_index
int readAFile(char *fileParam, int *current_index, int buffer_size)
{
    FILE *file = NULL;
    int file_size = 0;


    file = fopen(fileParam, "rb");
    if (file == NULL)
    {
        printf("Failed to open the file.\n");
        exit(EXIT_FAILURE);
        return -1;
    }


    fseek(file, 0, SEEK_END);
    file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Skip non-alphanumeric characters
    char ch1;
    while ((ch1 = fgetc(file)) != EOF)
    {
        if (isalnum(ch1))
        {
            fseek(file, -1, SEEK_CUR);  // Move back by 1 byte
            break;
        }
    }
    

    if (*current_index >= file_size)
    {
        fclose(file);
        file = NULL;
        file_size = 0;
        return -2;
    }

    fseek(file, 0L, SEEK_SET); 
    fseek(file, *current_index, SEEK_CUR);
    char ch;
    char buffer[buffer_size + 1];
    int buffer_index = 0;
    int number;
    int num_of_special = 0;

    while ((ch = fgetc(file)) != EOF && buffer_index <= buffer_size)
    {
        if (isdigit(ch) || ch == '\n') //https://piazza.com/class/lhaqnxludbb4e3/post/323 
        {
            buffer[buffer_index++] = ch;
        }
        else
        {
            num_of_special++;
        }
    }
    buffer[buffer_index] = '\0';

    // plus number of special charecters as we are not including them in buffer size
    *current_index = *current_index + buffer_size + num_of_special;

    int filtered_index = 0;
    for (int i = 0; i < buffer_size; i++)
    {
        if (isalnum(buffer[i]))
        {
            buffer[filtered_index++] = buffer[i];
        }
    }
    buffer[filtered_index] = '\0';

    number = atoi(buffer);

    return number;
}

// function written for rounding up
int ceilFloat(float number) {
    int roundedNumber = (int)number;  // Truncate the decimal part

    if (number > roundedNumber)
        roundedNumber += 1;  // Increment the integer part if there was a decimal part

    return roundedNumber;
}

int roundRobinReadFiles(struct CurrentIndex *currentIndexes, int buffer_size, int start_index, int end_index, int *current_file, int thread_id, int index, int *files_finished, float *previous_values) {

    // Read the file at current_file position
    char *file_path = files[*current_file].path;
    
    // index of current file (where did we left off in this file)
    int actualCurrentIndexInFile = currentIndexes[*current_file].index;

    // read a buffer from that current index of the file
    int result = readAFile(file_path, &currentIndexes[*current_file].index, buffer_size);
    
    //if its one file per thread it means that we are done
    if(files_per_thread == 1){
        if(result == -2){
            return -2;
        }
    }

    // If end of file, mark the file as done
    if (result == -2) {
        if(currentIndexes[*current_file].isDone != 1){
            currentIndexes[*current_file].isDone = 1;
            *files_finished++;
        } 
    }

    if(*files_finished == files_per_thread){
        return -2;
    }

    // Find the next file to read, that is not marked as done, guarantee to exist as not all files finished
    if(currentIndexes[*current_file].isDone == 1){
        while (currentIndexes[*current_file].isDone == 1) {
            (*current_file)++;
            if (*current_file >= end_index) { // If we've gone past the end, wrap around
                *current_file = start_index;
            }
        }
        char *file_path = files[*current_file].path;
        
        // since we are looking at a different file the current index needs to be updated
        actualCurrentIndexInFile = currentIndexes[*current_file].index;

        result = readAFile(file_path, &currentIndexes[*current_file].index, buffer_size);
    }

    if(result == -2){
        return result;
    }

    float alpha_value = 0;
    
    float fileAlpha = files[*current_file].alpha == 0.0 ? 1.0 : files[*current_file].alpha;
    float fileBeta = files[*current_file].beta == 0.0 ? 1.0 : files[*current_file].beta;


    
    if(actualCurrentIndexInFile == 0){ //if this is the first number in file
        alpha_value = (float) result;
    }
    else{
        alpha_value = low_pass_filter_value(fileAlpha, (float) result, previous_values[*current_file]);
    }

    float prev = previous_values[*current_file]; 
    previous_values[*current_file] = alpha_value;

    float beta_value = amplification_value(fileBeta, alpha_value);

    printf("thread: %d, file %d, original value: %d, prev alpha value: %f, alpha %f, beta %f, during iteration: %d\n", thread_id, *current_file, result,  prev, alpha_value, beta_value, index);


    // Move to the next file in the range [start_index, end_index)
    (*current_file)++;
    if (*current_file >= end_index) { // If we've gone past the end, wrap around
        *current_file = start_index;
    }

    // rounding the value up
    int final_value = ceilFloat(beta_value);
    return final_value;
}

void doThreadWork(int thread_id)
{
    int start_index = thread_id * files_per_thread;
    int end_index = start_index + files_per_thread;

    // strcpy(files[0].path, "/mnt/c/Users/teeya/Documents/school/cmpt300/assn3/input_files/file_1.txt");
    // strcpy(files[1].path, "/mnt/c/Users/teeya/Documents/school/cmpt300/assn3/input_files/file_2.txt");

    int file_value;
    int index = 0;
    bool has_been_counted = false;
    
    
    struct CurrentIndex *currentIndexes = malloc(num_files * sizeof(struct CurrentIndex));
    float *previous_values = malloc(sizeof(float) * num_files);

    int current_file = start_index;
    if (currentIndexes == NULL) {
        printf("Failed to allocate memory for currentIndexes.\n");
        return;
    }   
    if (previous_values == NULL) {
        printf("Failed to allocate memory for previous_values.\n");
        return;
    }   
    for (int i = 0; i < num_files; i++) {
        currentIndexes[i].index = 0;
        currentIndexes[i].isDone = 0;
    }

    for (int i = 0; i < num_files; i++) {
        previous_values[i] = 0.0;
    }

    int files_finished = 0;    

    while (num_threads != threads_finished)
    {   

        // no reading if thread is already finished
        if(has_been_counted){
           barrier_wait(&barrier);
            continue;
        }

        // get current file value
        file_value = roundRobinReadFiles(currentIndexes , buffer_size, start_index, end_index, &current_file, thread_id, index, &files_finished, previous_values);

        // Check if file_value is -2 (means we are at the end of file reading)
        if (file_value == -2) {
            if(!has_been_counted){
                // lock implemented to track the number of threads finished
                pthread_mutex_lock(&threads_finished_mutex);
                __sync_fetch_and_add(&threads_finished, 1);
                pthread_mutex_unlock(&threads_finished_mutex);
                // implement so we do not count the thread finished twice
                has_been_counted = true;
            }
        }
        // if we are not at the end of the file
        else{
            if (lock_config == 1){ // single global lock should be used for accessing the output channel
                if (file_value == -2){ // don't include -2 into the array
                    continue;
                }
                pthread_mutex_lock(&arrayAccessLock);
                printf("index: %d\n", index);
                printf("current Value: %d\n", file_value);
                printf("Value of array pre add: %d\n", modifiedArray[index]);
                int temp = modifiedArray[index] + file_value;
                // overflow handling
                if(temp < modifiedArray[index] ){
                    modifiedArray[index] = 65535;
                }
                else{
                    modifiedArray[index] = modifiedArray[index] + file_value;
                }
                printf("Value of array post add: %d\n\n", modifiedArray[index]);
                pthread_mutex_unlock(&arrayAccessLock);
            }
            else if (lock_config == 2 ){ //use a different lock for each entry in the output channel
                if (file_value == -2){ //don't include -2 into the array
                    continue;
                }
                printf("index: %d\n", index);
                printf("current Value: %d\n", file_value);
                printf("Value of array pre add: %d\n", modifiedArray[index]);
                lock_element(index);
                int temp = modifiedArray[index] + file_value;
                if(temp < modifiedArray[index] ){
                    modifiedArray[index] = 65535;
                }
                else{
                    modifiedArray[index] = modifiedArray[index] + file_value;
                }
                unlock_element(index);
                printf("Value of array post add: %d\n\n", modifiedArray[index]);

            }
            else if (lock_config == 3){ //use compare_and_swap to update the entries in the output channel
                if (file_value == -2){ //don't include -2 into the array
                    continue;
                }
                printf("index: %d\n", index);
                printf("current Value: %d\n", file_value);
                printf("Value of array pre add: %d\n", modifiedArray[index]);
                while (compare_and_swap(&CASLock, 0, 1) != 0){

                }
                int temp = modifiedArray[index] + file_value;
                if(temp < modifiedArray[index] ){
                    modifiedArray[index] = 65535;
                }
                else{
                    modifiedArray[index] = modifiedArray[index] + file_value;
                }
                printf("Value of array post add: %d\n\n", modifiedArray[index]);
                CASLock = 0;
            }
        }
        index++;
        // This is for output array (untill what point to we need to write into file)
        final_index = index;
        
        // Synchronixation
        barrier_wait(&barrier);
    } 

    // special case
    // thread0 has been waiting from before as it had the shorter information to parse
    // thread1 on its next iteration finishes its file and hits the barrier
    // thread0 does not go through the next iteration as threads finished == threads number
    // thread1 never exits
    if(barrier_get_count(&barrier) > 0){
        barrier_wait(&barrier);
    }
}

// no synchronization between threads
// in this version each thread read their files using RR algorithm but they do not wait at the end of their iterations
// this results in each thread finishing then other threads starting, thread 0 finished, thread 1 finished, .. (in my device)
void doThreadWork2(int thread_id)
{
    int start_index = thread_id * files_per_thread;
    int end_index = start_index + files_per_thread;

    // strcpy(files[0].path, "/mnt/c/Users/teeya/Documents/school/cmpt300/assn3/input_files/file_1.txt");
    // strcpy(files[1].path, "/mnt/c/Users/teeya/Documents/school/cmpt300/assn3/input_files/file_2.txt");

    int file_value = 0;
    int index = 0;    
    
    struct CurrentIndex *currentIndexes = malloc(num_files * sizeof(struct CurrentIndex));
    float *previous_values = malloc(sizeof(float) * num_files);

    int current_file = start_index;
    if (currentIndexes == NULL) {
        printf("Failed to allocate memory for currentIndexes.\n");
        return;
    }   
    if (previous_values == NULL) {
        printf("Failed to allocate memory for previous_values.\n");
        return;
    }   
    for (int i = 0; i < num_files; i++) {
        currentIndexes[i].index = 0;
        currentIndexes[i].isDone = 0;
    }

    for (int i = 0; i < num_files; i++) {
        previous_values[i] = 0.0;
    }

    // tracks how many files are finished
    int files_finished = 0;
    
    // while we are not at the end of value reading
    while (file_value != -2)
    {   

        file_value = roundRobinReadFiles(currentIndexes , buffer_size, start_index, end_index, &current_file, thread_id, index, &files_finished, previous_values);


        if (lock_config == 1){ //a single global lock should be used for accessing the output channel
            if (file_value == -2){ //don't include -2 into the array
                continue;
            }
            pthread_mutex_lock(&arrayAccessLock);
            printf("index: %d\n", index);
            printf("Value of file: %d\n", file_value);
            printf("Value of array pre add: %d\n", modifiedArray[index]);
            int temp = modifiedArray[index] + file_value;
            if(temp < modifiedArray[index] ){
                modifiedArray[index] = 65535;
            }
            else{
                modifiedArray[index] = modifiedArray[index] + file_value;
            }
            printf("Value of array post add: %d\n\n", modifiedArray[index]);
            pthread_mutex_unlock(&arrayAccessLock);
        }
        else if (lock_config == 2 ){ //use a different lock for each entry in the output channel
            if (file_value == -2){ //don't include -2 into the array
                continue;
            }
            printf("index: %d\n", index);
            printf("Value of file: %d\n", file_value);
            printf("Value of array pre add: %d\n", modifiedArray[index]);
            lock_element(index);
            int temp = modifiedArray[index] + file_value;
            if(temp < modifiedArray[index] ){
                modifiedArray[index] = 65535;
            }
            else{
                modifiedArray[index] = modifiedArray[index] + file_value;
            }
            unlock_element(index);
            printf("Value of array post add: %d\n\n", modifiedArray[index]);

        }
        else if (lock_config == 3){ //use compare_and_swap to update the entries in the output channel
            if (file_value == -2){ //don't include -2 into the array
                continue;
            }
            printf("index: %d\n", index);
            printf("Value of file: %d\n", file_value);
            printf("Value of array pre add: %d\n", modifiedArray[index]);
            while (compare_and_swap(&CASLock, 0, 1) != 0){

            }
            int temp = modifiedArray[index] + file_value;
            if(temp < modifiedArray[index] ){
                modifiedArray[index] = 65535;
            }
            else{
                modifiedArray[index] = modifiedArray[index] + file_value;
            }
            printf("Value of array post add: %d\n\n", modifiedArray[index]);
            CASLock = 0;
        }

        index++;
        final_index = index;
    } 
}

void find_files_per_thread(){
    files_per_thread = num_files/num_threads;
}

// middleware that decided if synchronization is needed based on input
void* threadFunction(void* arg)
{
    int thread_id = *((int*)arg);

    if(global_checkpointing == 0){
        doThreadWork2(thread_id);
    }
    else{
        doThreadWork(thread_id);
    }
    return NULL;
}

void createThreads(){
    threads = (pthread_t*)malloc(num_threads * sizeof(pthread_t)); //array to hold all threads
    if (threads == NULL){
        printf("failed malloc\n");
        exit (EXIT_FAILURE);
    }
    int* thread_ids = (int*)malloc(num_threads * sizeof(int)); // to see which thread is responsible for each file
    barrier_init(&barrier, num_threads);// synchronization for threads
  

    if (thread_ids == NULL)
    {
        printf("Failed to allocate memory for thread IDs.\n");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < num_threads; i++) {
        thread_ids[i] = i;
        int result = pthread_create(&threads[i], NULL, threadFunction, &thread_ids[i]);
        if (result != 0) {
            printf("Error creating thread %d.\n", i + 1);
            exit(EXIT_FAILURE);
        }
    }
    for (int i = 0; i < num_threads; i++) { // when thread work is done, rejoin
        pthread_join(threads[i], NULL);
    }
    free(thread_ids);
}

void parseMetadata()
{
    FILE *metadata_file = fopen(metadata_file_path, "r");
    if (metadata_file == NULL)
    {
        printf("Error: Failed to open metadata file.\n");
        exit(EXIT_FAILURE);
        return;
    }

    // Skip the BOM characters
    fgetc(metadata_file);
    fgetc(metadata_file);
    fgetc(metadata_file);

    char num_files_str[10];
    if (fgets(num_files_str, sizeof(num_files_str), metadata_file) == NULL)
    {
        printf("Error: Failed to read the number of input files from the metadata "
               "file.\n");
        fclose(metadata_file);
        exit(EXIT_FAILURE);
        return;
    }

    num_files = atoi(num_files_str);

    // file meta data information (maybe change the name of it)
    files = malloc(num_files * sizeof(struct FileEntry));

    int current_file = 0;
    char line[MAX_PATH_LENGTH];
    bool first = true;
    while (fgets(line, sizeof(line), metadata_file) != NULL &&
           current_file <= num_files)
    {
        line[strcspn(line, "\n")] = '\0'; // Remove newline character

        if (strstr(line, ".txt") != NULL)
        {
            strncpy(files[current_file].path, line, sizeof(files[current_file].path));
            first = true;
            current_file++;
        }
        else
        {
            if (first)
            {
                files[current_file - 1].alpha = atof(line);
                first = false;
                continue;
            }
            else
            {
                files[current_file - 1].beta = atof(line);
                first = true;
            }
        }
    }
    if(num_threads > num_files){
        num_threads = num_files;
    }
    find_files_per_thread();
    fclose(metadata_file);

    // Print the extracted information
    // printf("Number of input files: %d\n", num_files);
    // for (int i = 0; i < current_file; i++) {
    //     printf("File '%d' Path: %s\n", i+1, files[i].path);
    //     printf("Alpha '%d' value: %f\n", i+1, files[i].alpha);
    //     printf("Beta '%d' value: %f\n", i+1, files[i].beta);
    // }
}

// Function to ensure inputed arguments are valid, if they are valid, save them
void checkInput(int argc, char *argv[])
{
    if (argc < 7 || argc > 7)
    {
        printf("Error: Incorrect Number of Arguments Try Again\n");
        exit (EXIT_FAILURE);
    }
    buffer_size = atoi(argv[1]);
    num_threads = atoi(argv[2]);
    metadata_file_path = argv[3];
    lock_config = atoi(argv[4]);
    global_checkpointing = atoi(argv[5]);
    output_file_path = argv[6];
    if (atoi(argv[1]) == 0)
    {
        printf("Error: Invalid Buffer Size, Try Again\n");
        exit(EXIT_FAILURE);
    }
    if (atoi(argv[2]) == 0)
    {
        printf("Error: Invalid Number of Threads, Try Again\n");
        exit(EXIT_FAILURE);
    }
    if (atoi(argv[4]) != 1 && atoi(argv[4]) != 2 && atoi(argv[4]) != 3)
    {
        printf("Error: Invalid Lock Configuration, Try Again\n");
        exit(EXIT_FAILURE);
    }
    if (atoi(argv[5]) != 0 && atoi(argv[5]) != 1)
    {
        printf("Error: Invalid Global Checkpointing Configuration, Try Again\n");
        exit(EXIT_FAILURE);
    }
}

int main(int argc, char *argv[])
{
    checkInput(argc, argv);
    parseMetadata();

    //if lock config = 2, need to intialize the mutexes
    if (lock_config == 2){
        for (int i = 0; i < ARRAY_SIZE; i++) {
            if (pthread_mutex_init(&elementLock[i], NULL) != 0) {
                printf("Mutex initialization failed!\n");
                return 1;
            }
        }
    }
    
    //printf("The number of files per thread: %d\n", files_per_thread); //to remove, for testing
    createThreads();

    writeOutput();
    free(threads);
}