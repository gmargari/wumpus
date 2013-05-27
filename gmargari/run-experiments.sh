#!/bin/bash

SCRIPTDIR=`dirname $0`

#============================
# my_print()
#============================
scriptname=`basename $0`
my_print() {
    echo -e "\e[1;31m [ $scriptname ] $* \e[m"
}
    
#========================================================
# mailme()
#========================================================
mailme() {
#    echo $* | mail -s "message from `hostname`" gmargari@gmail.com
echo $*
}

#========================================================
# flush_page_cache ()
#========================================================
function flush_page_cache() {
#    free -o | grep -v ^Swap
    echo "sudo sync ..."
    sudo sync
    echo "echo 3 > /proc/sys/vm/drop_caches ..."
    sudo sh -c 'echo 3 > /proc/sys/vm/drop_caches' > /dev/null
}
        
#*************************************************************************
# build an index
#*************************************************************************
build_experiment ()
{
   # H sinaritisi pairnei 3 orismata
   if [ $# -ne 3 ]; then
      echo 'build_experiment(): wrong number of parameters';
      exit;
   fi

   fconfig=$1
   finput=$2
   mkdir -p $3
   foutput=$3/build

   echo -e "\n\n" $fconfig

   rm -rf ${SCRIPTDIR}/database/* &&
   flush_page_cache &&
   date +" %e %B %Y  %H:%M:%S" > $foutput &&
   time ${SCRIPTDIR}/../bin/wumpus --config=$fconfig < $finput >> $foutput &&
   date +" %e %B %Y  %H:%M:%S" >> $foutput &&
   echo "" >> $foutput &&
   ls -l ${SCRIPTDIR}/../database/ >> $foutput &&
   return

   err_msg="Wumpus error: cfg [$fconfig], input [$finput]"
   mailme $err_msg
   exit 1
}


#*************************************************************************
# search an index
#*************************************************************************
search_experiment ()
{
   # H sinaritisi pairnei 3 orismata
   if [ $# -ne 3 ]; then
      echo 'search_experiment(): wrong number of parameters';
      exit;
   fi

   fconfig=$1
   finput=$2
   mkdir -p $3
   foutput=$3/search

   echo -e "\n\n" $fconfig
   flush_page_cache
 
   date +" %e %B %Y  %H:%M:%S" > $foutput &&
   time ${SCRIPTDIR}/../bin/wumpus --config=$fconfig < $finput >> $foutput &&
   date +" %e %B %Y  %H:%M:%S" >> $foutput &&
   echo "" >> $foutput &&
   return

   err_msg="Wumpus error: cfg [$fconfig], input [$finput]"
   mailme $err_msg
   exit 1
}




#*************************************************************************
# main script starts
#*************************************************************************

F_PREFIX="tmp."
INPUT_FILES=./test_files
QUERIES=./test_queries
build_experiment   wumpus.cfg.HIM_NCA_stem3       $INPUT_FILES     ${F_PREFIX}HIM_NCA_stem3
search_experiment  wumpus.cfg.HIM_NCA_stem3       $QUERIES         ${F_PREFIX}HIM_NCA_stem3
exit



# grep "@0-Ok." TOIS.node31.NOMERGE_stem3.search | awk '{print substr($2,2,length($2))}' > NOMERGEsearch.tmp

F_PREFIX="TOIS.`hostname`."

INPUT_FILES=/home/gmargari/gov2_files/426Gb_wumpus
QUERIES=/index/proteus/search_queries/queries_1000.txt.wumpus

build_experiment   wumpus.cfg.HIM_NCA_stem3       $INPUT_FILES     ${F_PREFIX}HIM_NCA_stem3
search_experiment  wumpus.cfg.HIM_NCA_stem3       $QUERIES         ${F_PREFIX}HIM_NCA_stem3

build_experiment   wumpus.cfg.HSM_NCA_stem3       $INPUT_FILES     ${F_PREFIX}HSM_NCA_stem3
search_experiment  wumpus.cfg.HSM_NCA_stem3       $QUERIES         ${F_PREFIX}HSM_NCA_stem3

build_experiment   wumpus.cfg.HLM_NCA_stem3       $INPUT_FILES     ${F_PREFIX}HLM_NCA_stem3
search_experiment  wumpus.cfg.HLM_NCA_stem3       $QUERIES         ${F_PREFIX}HLM_NCA_stem3

build_experiment   wumpus.cfg.NOMERGE_stem3       $INPUT_FILES     ${F_PREFIX}NOMERGE_stem3
search_experiment  wumpus.cfg.NOMERGE_stem3       $QUERIES         ${F_PREFIX}NOMERGE_stem3

exit;


build_experiment  wumpus.cfg.NOMERGE_stem3        $INPUT_FILES     ${F_PREFIX}NOMERGE_stem3
build_experiment  wumpus.cfg.HIM_NCA_stem3        $INPUT_FILES     ${F_PREFIX}HIM_NCA_stem3
build_experiment  wumpus.cfg.HLM_NCA_stem3        $INPUT_FILES     ${F_PREFIX}HLM_NCA_stem3
build_experiment  wumpus.cfg.HSM_NCA_stem3        $INPUT_FILES     ${F_PREFIX}HSM_NCA_stem3

exit;



build_experiment  wumpus.cfg.HSM_NCA_stem0        $INPUT_FILES     ${F_PREFIX}HSM_NCA_stem0
build_experiment  wumpus.cfg.HSM_NCA_stem1        $INPUT_FILES     ${F_PREFIX}HSM_NCA_stem1
build_experiment  wumpus.cfg.HSM_NCA_stem2        $INPUT_FILES     ${F_PREFIX}HSM_NCA_stem2
build_experiment  wumpus.cfg.HSM_NCA_stem3        $INPUT_FILES     ${F_PREFIX}HSM_NCA_stem3

build_experiment  wumpus.cfg.HIM_NCA_stem0        $INPUT_FILES     ${F_PREFIX}HIM_NCA_stem0
build_experiment  wumpus.cfg.HIM_NCA_stem1        $INPUT_FILES     ${F_PREFIX}HIM_NCA_stem1
build_experiment  wumpus.cfg.HIM_NCA_stem2        $INPUT_FILES     ${F_PREFIX}HIM_NCA_stem2
build_experiment  wumpus.cfg.HIM_NCA_stem3        $INPUT_FILES     ${F_PREFIX}HIM_NCA_stem3

build_experiment  wumpus.cfg.HLM_NCA_stem0        $INPUT_FILES     ${F_PREFIX}HLM_NCA_stem0
build_experiment  wumpus.cfg.HLM_NCA_stem1        $INPUT_FILES     ${F_PREFIX}HLM_NCA_stem1
build_experiment  wumpus.cfg.HLM_NCA_stem2        $INPUT_FILES     ${F_PREFIX}HLM_NCA_stem2
build_experiment  wumpus.cfg.HLM_NCA_stem3        $INPUT_FILES     ${F_PREFIX}HLM_NCA_stem3
