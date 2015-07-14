#!/bin/bash

rm -f results
rm -f tblResults

percents="8 10 20 50"
threads="1 2 4 8 16 32"
#threads="64"
algorithms="bhop hopd hopnd"
capacities="100000000"
tblpers="10 50 60 90"

count=0
rep="1 2 3:"
rep="1"

for algorithm in $algorithms; do
for tblper in $tblpers; do
for capacity in $capacities; do
for percent in $percents; do
for thread in $threads; do
for rep1 in $rep; do
	count=$(($count + 1))
	line="$algorithm $count $thread $percent $tblper $capacity 2 1"
	line=`echo $line | sed 's/_/ /g'`
	echo "$line" 1>&2
	echo -n "$line " >> results
        ./test_intel64 $line  >> results
#	echo >> results
done; done; done; done; done; done;

#cat results | grep lwp_exit | awk '{print $5}' > cache_miss.txt
