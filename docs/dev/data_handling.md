# Data handling

## Data Acquisition

Data is sampled/replayed through the ADC (`TI ADS8691`) and DAC (`TI DAC8562T`). Both devices are interfaced over a custom, SPI-compatible protocol. For a detailed description of the protocol and timing requirements, refer to the corresponding datasheets. The protocol is bit-banged using the low-latency GPIOs connected to PRU0. The transfer routines itself are [implemented in assembly](https://github.com/orgua/shepherd/blob/main/software/firmware/lib/src/spi_transfer_pru.asm).

## PRU to host

Data is sampled and bidirectionally transferred between PRUs and user space in buffers. These buffers correspond to sections of a continuous area of memory in DDR RAM to which both PRUs and user space application have access. This memory is provisioned through `remoteproc`, a Linux framework for managing resources in an AMP (asymmetric multicore processing) system. The PRU firmware contains a so-called resource table that allows to specify required resources. We request a carve-out memory area, which is a continuous, non-cached area in physical memory. On booting the PRU, the `remoteproc` driver reads the request, allocates the memory and writes the starting address of the allocated memory area to the resource table, which is readable by the PRU during run-time. The PRU exposes this memory location through shared RAM, which is accessible through the sysfs interface provided by the kernel module. Knowing physical address and size, the user space application can map that memory after which it has direct read/write access. The total memory area is divided into three distinct buffers for ivsamples, gpio-traces and pru-utilization-log.

The shared RAM approach is the fastest option on the BeagleBone, but still has some caveats. Writing from PRU-side to DDR RAM can be done within one cycle. The operation does not finish in that time, but does not block the PRU. Reading on the other hand can take several hundred cycles. In rare cases `> 4 us`, which equals 800 cycles or almost half the real-time window. For that reason the reading operation is done by PRU1, as it has more resources to spare. A future design should consider, that reading takes almost same time for 1 byte or 100 bytes.

:::{note}
This design will switch to a large cyclic buffer in the near future to reduce overhead (buffer exchanges) for the PRU.
:::

In the following we describe the data transfer process for emulation. Emulation is the most general case because harvesting data has to be transferred from a database to the analog frontend, while simultaneously data about energy consumption (target voltage and current) & gpio traces have to be transferred from the analog frontend (ADC) to the database.

The userspace application writes the first block of harvesting data into one of the (currently 64) buffers, e.g. buffer index 0. After the data is written, it sends a message to PRU0, indicating the message type (`MSG_BUF_FROM_HOST`) and index (0). The PRU0 receives that message and stores the index in a ringbuffer of empty buffers. When it's time, PRU0 retrieves a buffer index from the ringbuffer and reads the harvesting values (current and voltage) sample by sample from the buffer, sends it to the DAC and subsequently samples the 'load' ADC channels (current and voltage), overwriting the harvesting samples in the buffer. Once the buffer is full, the PRU sends a message with type (`MSG_BUF_FROM_PRU`) and index (i). The userspace application receives the index, reads the buffer, writes its content to the database and fills it with the next block of harvesting data for emulation.

### Data extraction

The user space code (written in python) has to extract the data from a buffer in the shared memory. Generally, a user space application can only access its own virtual address space. We use Linux's `/dev/mem` and python's `mmap.mmap(..)` to map the corresponding region of physical memory to the local address space. Using this mapping, we only need to seek the memory location of a buffer, extract the header information using `struct.unpack()` and interpret the raw data as numpy array using `numpy.frombuffer()`.


## Database

In the current local implementation, all data is locally stored in the filesystem. This means, that for emulation, the harvesting data is first copied to the corresponding shepherd node. The sampled data (harvesting data for recording and energy traces for emulation) is also stored on each individual node first and later copied and merged to a central database. We use the popular HDF5 data format to store data and meta-information.

See Chapter [](../user/data_format) for more details.
