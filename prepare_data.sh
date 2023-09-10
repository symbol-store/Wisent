#~/bin/sh

# requirements

if ! [ -x "$(command -v wget)" ]; then
   echo 'Error: wget is missing!' >&2
   exit 1
else
	echo "wget found."
fi

if ! [ -x "$(command -v python3)" ]; then
   echo 'Error: python3 is missing!' >&2
   exit 1
else
	echo "python3 found."
fi

if python3 -c "import pandas" &> /dev/null; then
	echo "python3 pandas found."
else
   echo 'Error: python3 pandas is missing!' >&2
   exit 1
fi

if python3 -c "import json" &> /dev/null; then
	echo "python3 json found."
else
   echo 'Error: python3 json is missing!' >&2
   exit 1
fi

# OWID

mkdir -p Data/owid-deaths
cd Data/owid-deaths
wget -O datapackage.json https://raw.githubusercontent.com/owid/owid-datasets/master/datasets/20th%20century%20deaths%20in%20US%20-%20CDC/datapackage.json
if ! [ -e 20th\ century\ deaths\ in\ US\ -\ CDC.csv ]; then
	wget https://raw.githubusercontent.com/owid/owid-datasets/master/datasets/20th%20century%20deaths%20in%20US%20-%20CDC/20th%20century%20deaths%20in%20US%20-%20CDC.csv
fi

python3 ../removeAlternatives.py datapackage.json
python3 ../sizeVariants.py 20th\ century\ deaths\ in\ US\ -\ CDC.csv

cd ../..

# OPSD (weather)

mkdir -p Data/opsd-weather
cd Data/opsd-weather
wget -O datapackage.json https://data.open-power-system-data.org/weather_data/2019-04-09/datapackage.json
if ! [ -e weather_data.csv ]; then
	wget https://data.open-power-system-data.org/weather_data/2019-04-09/weather_data.csv
fi

python3 ../removeAlternatives.py datapackage.json
python3 ../sizeVariants.py weather_data.csv

cd ../..
