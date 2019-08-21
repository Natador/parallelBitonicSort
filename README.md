# Parallel Bitonic Sort
Task 6 in my parallel processing course which implements Bitonic Sort on an MPI machine

## Dependencies, Installation, and Use
parallelBitonicSort requires [MPICH](https://www.mpich.org/) to run. On Ubuntu or Debian, this can be accomplished by:

```bash
apt-get install mpich
```

which installs a metapackage with all the required MPICH components. The `mpich-doc` package is also useful for MPICH documentation.

Ensure this properly installs. The commands needed are `mpicc`, which is the MPI compiler, and `mpiexec`. Either of these can be run with the `--version` flag to verify the installation.

### Running as-is
To build and run the software, simply execute the commands

```bash
make
make run [size]
```

with the optional `size` argument being a power of 2. If the `size` argument is not provided, the default size of 1024 will be used.

### Changing the Number of Tasks
To change the number of tasks, 2 steps are involved.

**NOTE**: the number of tasks and size of the array to be sorted must both be a power of 2. Changing these to non-powers of 2 results in undefined behavior.

1. Change the number of nodes in the `mpiexec` command in the makefile as follows:
```make
mpiexec -n [num_nodes] ./bitonic
```

2. Change the `TASKS` constant in `bitonic.c` to the number of tasks, and the `STEPS` constant to `log2(TASKS)`. E.g., if `TASKS` is 8, then `STEPS` is 3, etc.
