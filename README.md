
# Wisent Data Serialization/Deserialization + Benchmarks

This is the code for the paper "Wisent: An In-Memory Serialization Format for Leafy† Trees", H. Mohr-Daurat, H. Pirk

The project is composed of several applications:

* WisentServer

Implements the serialization of JSON files and CSV files into the Wisent format and stores the data in shared memory. It also handles loading into JSON sting buffer and BSON binary buffer. The client benchmark application reads the data from shared memory and performs the deserialization. Communication between clients and the server is done through http requests.

* WisentBenchmarks

This application contains the benchmarks for the C++ Wisent deserializer as well as for all the baselines (using various JSON libraries).

* Python Benchmark (Benchmarks/Python/Aggregation.py)

This is one example Python application to deserialize Wisent data and performs an aggregation query (same query as in the C++ implementation).

* Swift Benchmark (Benchmarks/Swift)

This is one example Swift application to deserialize Wisent data and performs an aggregation query (same query as in the C++ implementation).

## Example Wisent File

The 'Example' folder contains the Wisent example file from the paper: 'example.wisent', generated from this JSON document:
```
{
  "Data": {
    "File" : {
      "Content": [
        0.17,
        91.0,
        4.20,
        0.45
      ],
    "Name": "Population.csv"
    }
  },
  "Id": 17,
  "Title": "Population"
}
```
To note: a slight difference from the example in the paper, JSON's `{}` is translated in an `Object` node and JSON's `[]` is translated in a `List` node.
This is required to support unnamed JSON objects (since the current implementation of Wisent requires a name for each node).

This file can be used as a reference to implement a new deserializer.
In addition to the four buffers described in the paper, the file starts with an header containing the following data:
```
argumentCount (8 bytes): number of elements in the Argument Vector (and the Type Vector), buffer size = argumentCount * 8 bytes
expressionCount (8 bytes): number of elements in the Structure Vector, buffer size = expressionCount * 3 * 8 bytes
originalAddress (8 bytes): internal
stringArgumentsFillIndex (8 bytes): size of the string buffer
```

## Requirements

For compiling WisentServer, and WisentBenchmarks:
```
cmake >= 3.10
clang >= 9.0
boost interprocess
```

For preparing the data:
```
wget
python3
python3 pandas
python3 json
```

## Instructions (for Linux Ubuntu/Debian)

### 1) configuring and compiling project

```
> mkdir build
> cd build
> cmake -DCMAKE_C_COMPILER=clang   -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release -B. ..
> cd ..
> cmake --build build --target install
```

### 2) generating the data

```
> ./prepare_data.sh
```

### 3) run the benchmarks

start the Wisent Server (the workspace folder matters):
```
> cd build
> ./WisentServer &
> cd ..
```

start the C++ benchmark (Wisent and baselines):
```
> build/WisentBenchmarks
```

start the Python benchmark:
```
> python3 Benchmarks/Python/Aggregation.py
```

### 4) manual requests to Wisent Server

This is done through http requests.
For the following commands, replace for example [dataset] with 'opsd' and [pathname] with '../Data/opsd-weather/'.

* Load [dataset] from [pathname] into Wisent format
> http://localhost:3000/load?name=[dataset]&path=[pathname]

* Load [dataset] from [pathname] into JSON (without embedded CSV data)
> http://localhost:3000/load?name=[dataset]&path=[pathname]&toJson&loadCSV=0

* Load [dataset] from [pathname] into JSON (with embedded CSV data)
> http://localhost:3000/load?name=[dataset]&path=[pathname]&toJson

* Load [dataset] from [pathname] into BSON (with embedded CSV data)
> http://localhost:3000/load?name=[dataset]&path=[pathname]&toBson

* Unload [dataset] from the server process
> http://localhost:3000/unload?name=[dataset]

* Erase [dataset] from shared memory
> http://localhost:3000/erase?name=[dataset]

* Stop the server
> http://localhost:3000/stop

### Additional command line options for the Wisent Server

Change the http port (default 3000):
> --http-port XX

Disable Run-Length Encoding (enabled by default):
> --disable-rle
