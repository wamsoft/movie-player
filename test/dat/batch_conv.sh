#!/usr/bin/bash

if [ $# -lt 2 ]; then
  echo "usage: $0 <src_movie> <length_sec>" 1>&2
  exit 1
fi

FFMPEG=ffmpeg
SOURCE=$1
LENGTH=$2
SOURCE_FILE=${SOURCE##*/}
SOURCE_BASE=${SOURCE_FILE%.*}

VCODECS="vp8 vp9"
ACODECS="libvorbis libopus"

function conv() {
  vcodec=$1
  acodec=$2
  outname="${SOURCE_BASE}_${vcodec}_${acodec##lib}_${LENGTH}s.webm"

  echo "generate: $outname"
  ${FFMPEG} -i ${SOURCE} -t ${LENGTH} -c:v ${vcodec} -c:a ${acodec} ${outname}
}

for v in ${VCODECS}; do
  for a in ${ACODECS}; do
    conv "$v" "$a"
  done
done

