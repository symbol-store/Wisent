#!/bin/bash

method=( Wisent JsonCsv Json Bson RapidJson SimdJson )
size=( _div256 _div128 _div64 _div32 _div16 _div8 _div4 _div2 _scale1 )

echo -e "Method\tScale\tVmHWM"
for i in "${method[@]}" ; do
   for j in "${size[@]}" ; do
	  WSL-Clang-RelDeb/Benchmarks --benchmark_filter="^$i,opsd-weather,size:$j,selectivity:1/1$" --benchmark_min_time=5s > /dev/null 2>&1 &
      while [ -e /proc/$! ]; do
	      VmHWM=$(grep VmHWM /proc/$!/status | grep -oEi [0-9]+)
		  sleep 1
      done
      echo -e "$i\t$j\t$VmHWM"
   done
done
