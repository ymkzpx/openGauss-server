#!/bin/bash
tools_path=/home/PrivateBuild_tools
agent_path=/usr1/gauss_jenkins/jenkins/workspace/openGauss/
log_path=/home/log
check_nu=$1
#################################### create dir and logfile###########################################
test -d $log_path && rm -rf $log_path
mkdir $log_path
g_lltlog_dir=${tools_path}/log
g_lltlog_file=${g_lltlog_dir}/makemppdb_llt_${check_nu}_$(date +'%Y%m%d_%H%M%S').log
mkdir -p ${g_lltlog_dir}
#################################### modify config.ini################################################
#sed -i "/^g_boot_dir=/c\g_boot_dir=${agent_path}"   /home/PrivateBuild_tools/config.ini

date
#################################### merge modify ##################################################
# echo merge_modify_start_time=$(date +%s.%N) >> ${g_lltlog_file} 2>&1
# sh  $tools_path/Private_Get_Modify.sh
# if [ $? -ne "0" ];then
    # printf "Failed to download the personal branch code! Error:\n"
    # [ -f "${log_path}/pullerror" ] && cat ${log_path}/pullerror
    # exit 1
# fi
# date
# echo merge_modify_stop_time=$(date +%s.%N) >> ${g_lltlog_file} 2>&1
#echo "[`date +'%Y-%m-%d %H:%M:%S'`] [openGauss] INFO: Command: cd ${agent_path} && git submodule init && git submodule update --remote && cd $agent_path/binarylibs && git lfs pull"
#cd $agent_path
#git submodule init
#git submodule update --remote
#cd $agent_path/binarylibs
#git lfs pull
#################################### check llt part ##################################################
if [ "${check_nu}" == "4" ] ; then
    printf "Only check upgracheck in the LLT part 4\n"
    check_mode='fastcheck'
elif [ "${check_nu}" == "0" ] ; then
    check_mode='fastcheck_single'
elif [ "${check_nu}" == "16" ] ; then
    check_mode='fastcheck_single_mot'
else
    #check_mode='fastcheck_single'
    check_mode='fastcheck'
fi

#################################### do llt job ##################################################
echo llt_job_start_time=$(date +%s.%N) >> ${g_lltlog_file} 2>&1
# grep '|'  $log_path/build_incr_file |grep -v "Test/mppcases_c10" |grep -v "script/script" | awk  '{print $1}' >  $log_path/incr_file

# if [ `cat $log_path/incr_file |wc -l` -gt 0 ];then
    sh $tools_path/Private_LLT_Build.sh -m $check_mode -p $check_nu -c ${g_lltlog_file}
    if [ $? -ne "0" ];then
        printf "Failed to make fastcheck! Please check the above logs to see the reason\n"
        exit 1
    fi
# fi
echo llt_job_stop_time=$(date +%s.%N) >> ${g_lltlog_file} 2>&1
date

printf "====================================  Do LLT END  ========================================\n"

