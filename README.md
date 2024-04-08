# Overview

CXL Switch Emulator (CSE) is a software application that emulates the 
management path functionality of a CXL switch. It is compliant with the CXL 
Fabric Management API of the CXL 2.0 specification. All CXL FM management 
commands are sent using MCTP. 

CSE listens for connections from a Fabric Manager (FM) over TCP on a default 
port of 2508. Only one remote connection is supported at a time. After a 
connection is terminated by the remote Fabric Manager, CSE will wait for
another connection. 

# Supported Operating System Versions

- Ubuntu 23.10
- Fedora 38, 39

> Note: Ubuntu 22.04 is not supported. This is due to some newer PCI features that
> are missing from the 5.15 Linux kernel that ships with Ubuntu 22.04.

# Building

1. Install OS libraries

Install the following build packages to compile the software on the following
operating systems.

**Ubuntu:**

```bash
apt install build-essential libglib2.0-dev libyaml-dev libpci-dev
```

**Fedora:**

```bash
```

2. Build Dependencies

To clone and build dependencies run:

```bash
./builddeps.bash
```

3. Build

After building the required dependencies run:

```bash
make
```

# Usage

1. Create host directory for memory mapped files

CSE primarily emulates the management path functionality of a CXL switch and 
does not emulate PCIe/CXL data path functionality.  However, some CXL FM API 
management commands enable the Fabric Manager to read or write to the memory 
space of CXL Type-3 devices. To emulate these commands, CSE can be configured 
to instantiate memory mapped files as backing stores. This will allow a Fabric 
Manager such as [Jack](https://github.com/JackrabbitLabs/jack) to write to an 
arbitrary location in the memory space of a CXL Type-3 device, and then read 
back the contents of those locations. 

If the user does not wish to use the CXL LD CXL.io Memory Request commands, 
this step can be skipped. 

The config.yaml file has a parameter that specifies the location of a folder 
in the host file system where the memory mapped files will be located. The 
default location is `/cxl`. This directory must exist if the user plans to use
the CXL LD CXL.io commands. 

While the target directory (e.g. /cxl) can reside on the root file system of 
the host, it is recommended to use a tmpfs file system which can be created 
with the following command:

```bash
sudo mkdir /cxl 
sudo mount -t tmpfs -o size=4G,mode=1777 cxl /cxl 
```

This should be performed *before* starting the CSE application. 

The capacity of the tmpfs file system is dependent up on how much of the CXL 
Type-3 device memory space needs to be written/read. The memory mapped files
are sparse allocated and will only consume the actually written capacity on
the tmpfs file system. 

> Note: memory mapped files are only created for virtual CXL device profiles 
> that have a `mmap: 1` in their profile definition in the config.yaml file. 

2. Start the CSE application 

The CSE application can be launched with the following command: 

```bash
cse -lc config.yaml
```

The `-l` flag will configure CSE to produce log level output that states the 
commands received and actions taken. 

3. Exit 

To exit the application, type `CTRL-C`.

