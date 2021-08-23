#!/bin/bash
# Copyright 2020 Huawei Technologies Co., Ltd
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
# ============================================================================

if [ $# != 4 ]
then
    echo "Usage: sh run_eval.sh [DATASET_PATH] [VAL_IMG_DIR] [VAL_JSON_FILE] [CHECKPOINT_PATH]"
exit 1
fi

get_real_path(){
  if [ "${1:0:1}" == "/" ]; then
    echo "$1"
  else
    echo "$(realpath -m $PWD/$1)"
  fi
}
DATASET_PATH=$(get_real_path $1)
VAL_IMG_DIR=$2
VAL_JSON_FILE=$3
CHECKPOINT_PATH=$(get_real_path $4)

echo $DATASET_PATH
echo $CHECKPOINT_PATH

if [ ! -d $DATASET_PATH ]
then
    echo "error: DATASET_PATH=$PATH1 is not a directory"
exit 1
fi

if [ ! -f $CHECKPOINT_PATH ]
then
    echo "error: CHECKPOINT_PATH=$PATH2 is not a file"
exit 1
fi

export DEVICE_NUM=1
export DEVICE_ID=0
export RANK_SIZE=$DEVICE_NUM
export RANK_ID=0

if [ -d "eval" ];
then
    rm -rf ./eval
fi
mkdir ./eval
cp ../*.py ./eval
cp ../*.yaml ./eval
cp -r ../src ./eval
cp -r ../model_utils ./eval
cd ./eval || exit
env > env.log
echo "start inferring for device $DEVICE_ID"
python eval.py \
    --data_dir=$DATASET_PATH \
    --val_img_dir=$VAL_IMG_DIR \
    --val_json_file=$VAL_JSON_FILE \
    --is_distributed=0 \
    --per_batch_size=1 \
    --pretrained=$CHECKPOINT_PATH > log.txt 2>&1 &
cd ..
