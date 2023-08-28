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
PIXFORMATS="yuv420p yuv422p yuv444p"

function conv() {
  vcodec=$1
  acodec=$2
  pixfmt=$3
  outname="${SOURCE_BASE}_${vcodec}_${acodec##lib}_${pixfmt}_${LENGTH}s.webm"

  echo "generate: $outname"
  ${FFMPEG} -i ${SOURCE} -t ${LENGTH} -c:v ${vcodec} -c:a ${acodec} -pix_fmt ${pixfmt} ${outname}
}

for v in ${VCODECS}; do
  for a in ${ACODECS}; do
    for f in ${PIXFORMATS}; do
      conv "$v" "$a" "$f"
    done
  done
done

