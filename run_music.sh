#!/usr/bin/env bash
#PARAMS="$1"
PARAMS="./param/parameters.ini"
RUN_HOMEL=1

echo "Cleaning up previous runs..."
rm -rf ../fields/*
rm -rf ../output/*
echo "Done"
./MUSIC $PARAMS
./bin/filter_gen $PARAMS
if [[ $RUN_HOMEL == 1 ]]; then
  ./bin/hpkvd 1 13579 $PARAMS
fi
echo "Running HPVKD..."
./bin/hpkvd 0 13579 $PARAMS
echo "Running MERGE_PVKD..."
./bin/merge_pkvd 13579 $PARAMS
