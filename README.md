# ThreadChannelsProgram-
Application is designed as a multi-threaded C program that processes numerical data from various input channels, combining them into a single output after applying optional signal processing techniques. It reads from multiple files, each representing a channel with sequences of 16-bit integers, and performs low-pass filtering and amplification.
# README for Assignment 3: myChannels

## Overview

`myChannels` is a multi-threaded C program designed to process numerical data from multiple input channels, applying optional low-pass filtering and amplification before summing them to produce a final output. The program handles input from files containing 16-bit integers, each representing a discrete numerical sample from different channels. The objective is to demonstrate proficiency in multi-threaded programming, buffered file I/O, and synchronization mechanisms in a POSIX-compliant environment.

## Features

- **Multi-Threaded Processing**: Utilizes multiple threads to concurrently process numerical samples from different channels, efficiently handling CPU-bound tasks.
- **Buffered File Reading**: Implements buffered reading to minimize disk I/O operations, enhancing performance by reading a specified number of bytes (buffer_size) at a time.
- **Low-Pass Filtering**: Each channel can be processed with an optional low-pass filter, blending each sample with its predecessor to produce a smoothed output.
- **Amplification**: Allows for the optional amplification of samples, adjusting their magnitude by a specified factor.
- **Flexible Configuration**: Supports configuration through a metadata file specifying the number of channels, paths to channel files, and processing parameters (low-pass filter value and amplification factor).
- **Synchronization**: Employs mutex locks and conditional variables to synchronize access to shared resources and ensure thread-safe operations.
- **Checkpointing**: Incorporates local and global checkpointing mechanisms to control the progress of threads, ensuring orderly processing of numerical samples.

## Usage

### Compilation

Use the provided Makefile for compilation:

```sh
make all
```

This command compiles the source code and produces an executable named `myChannels`.

### Running the Program

```sh
./myChannels <buffer_size> <num_threads> <metadata_file_path> <lock_config> <global_checkpointing> <output_file_path>
```

#### Arguments

- `buffer_size`: The size of the buffer (in bytes) for reading files.
- `num_threads`: The number of threads to create for processing.
- `metadata_file_path`: Absolute path to the metadata file describing input channels and their processing parameters.
- `lock_config`: Configuration for locking mechanisms (1 for a global lock, 2 for granular locks, 3 for compare-and-swap).
- `global_checkpointing`: Flag to enable/disable global checkpointing (1 for enabled, 0 for disabled).
- `output_file_path`: Path to the output file where the final processed samples will be stored.

### Metadata File Format

The metadata file should follow this format:

```
<number_of_input_files>
<channel_file1_path>
<channel_file1_low_pass_filter_value>
<channel_file1_amplification>
...
```

### Example

Given two input files and a metadata file specifying their paths and processing parameters, `myChannels` will process each file according to the specified low-pass filter and amplification values, summing up corresponding samples from all channels and writing the final output to the specified output file.

## Implementation Details

- **Thread Allocation**: Each thread is assigned an equal portion of the total number of channel files to process. The program ensures that the total number of input files is a multiple of the number of threads.
- **Buffered I/O**: The program reads from input files using a buffer of size `buffer_size`, minimizing the number of read operations required.
- **Data Processing**: Each sample is first processed through the optional low-pass filter and then amplified if required. The processed samples from all channels are summed to produce the final output.
- **Synchronization**: Depending on the `lock_config` setting, the program uses either a global lock, granular locks, or atomic operations to ensure thread-safe updates to the shared output data.
- **Checkpointing**: The program uses either local or global checkpointing based on the `global_checkpointing` flag to manage the progress of threads and ensure orderly processing.
To explain how round-robin scheduling is used in the context of processing input files with the given example, let's break down the operations and how they would be distributed among multiple threads in a round-robin fashion. The example provided illustrates the processing of numerical values from a single input file (input file 2), applying a low-pass filter and amplification to each value.

### Round-Robin Scheduling Overview

Round-robin scheduling is a method used to distribute tasks evenly across multiple workers (in this case, threads). Each thread gets a turn to process a portion of the task before passing the next portion to the next thread in sequence. This continues until all tasks are completed. The goal is to ensure fair use of resources and prevent any single thread from becoming a bottleneck.

### Application to the Example

Given the example operations on input file 2:

1. **Initial Samples**: 10, 20, 30
2. **Low-Pass Filter Applied**:
   - For the first sample (10), it's simply taken as is because there's no previous value, resulting in 10.
   - For the second sample (20), the low-pass filter formula is applied: `new_value = 0.5 * current_value + 0.5 * previous_value`, resulting in 6.
   - For the third sample (30), the same formula is applied, using the new value of the second sample as the "previous" value, resulting in 3.
3. **Amplification Applied**:
   - Each of the new sample values from the low-pass filter step is then multiplied by the amplification factor (0.5 in this case).

### Round-Robin Distribution

In a multi-threaded scenario using round-robin scheduling, each thread would be assigned a portion of the file(s) to process based on the number of threads and the total workload. For simplicity, let's assume two threads (Thread 1 and Thread 2) and two input files (File 1 and File 2), each with an equal number of samples.

- **Thread 1** starts processing the first sample of File 1, applies the low-pass filter and amplification, and then moves to the first sample of File 2.
- **Thread 2** waits for Thread 1 to complete its first sample processing. Once Thread 1 moves to File 2, Thread 2 starts processing the second sample of File 1.
- This process repeats, with each thread taking turns processing a sample from its assigned file(s) in a round-robin manner.

### Ensuring Synchronization

To prevent data races and ensure that each thread can safely apply the low-pass filter (which depends on the previous sample value), synchronization mechanisms such as mutexes or locks would be used. When a thread is updating a shared resource (e.g., the previous sample value or the final output array), it would lock that resource, perform the update, and then unlock it for the next thread.

### Handling Checkpointing

With round-robin and local checkpointing, each thread must complete processing its current set of samples before any thread can proceed to the next set. This ensures that the low-pass filter's dependency on the "previous sample value" is reliably maintained across all threads.

In summary, round-robin scheduling in this context ensures that each thread takes turns processing a part of the workload, with synchronization mechanisms in place to handle dependencies between consecutive samples. This approach balances the workload across multiple threads, improving the program's overall efficiency and throughput.

## Limitations

- The program expects the number of input files to be a multiple of the number of threads.
- All input files must contain 16-bit integer samples, and each sample must be greater than 0.
- The buffer size must be sufficient to accommodate at least one complete sample and potentially part of the next, including any line feed characters.

## Conclusion

`myChannels` demonstrates the application of multi-threaded programming concepts in C, including thread synchronization, buffered I/O, and the use of POSIX APIs. It showcases how to efficiently process numerical data from multiple sources in parallel, applying signal processing techniques and producing a combined output.
