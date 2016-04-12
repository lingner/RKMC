/*
 *      Copyright (C) 2005-2013 Team XBMC
 *      http://xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "DVDDemuxFFmpegRK.h"

#include "commons/Exception.h"
#include "cores/FFmpeg.h"
#include "cores/dvdplayer/DVDClock.h" // for DVD_TIME_BASE
#include "cores/dvdplayer/DVDDemuxers/DVDDemuxUtils.h"
#include "cores/dvdplayer/DVDInputStreams/DVDInputStream.h"
#include "cores/dvdplayer/DVDInputStreams/DVDInputStreamFFmpeg.h"
#include "filesystem/CurlFile.h"
#include "filesystem/Directory.h"
#include "filesystem/File.h"
#include "settings/AdvancedSettings.h"
#include "settings/Settings.h"
#include "system.h"
#include "threads/SystemClock.h"
#include "URL.h"
#include "utils/log.h"
#include "utils/StringUtils.h"


CDVDDemuxFFmpegRK::CDVDDemuxFFmpegRK() 
{
  m_bSSIF = false;
  m_bNeedMVC = false;
}

CDVDDemuxFFmpegRK::~CDVDDemuxFFmpegRK()
{
    
}

DemuxPacket* CDVDDemuxFFmpegRK::Read()
{
  DemuxPacket* pPacket = NULL;
  // on some cases where the received packet is invalid we will need to return an empty packet (0 length) otherwise the main loop (in CDVDPlayer)
  // would consider this the end of stream and stop.
  bool bReturnEmpty = false;
  { CSingleLock lock(m_critSection); // open lock scope
  if (m_pFormatContext)
  {
    // back mvc packet
    if (m_bSSIF && m_bNeedMVC)
    {
      m_bNeedMVC = false;
      if (!m_SSIFqueue.empty())
      {
        DemuxPacket* mvcpkt = m_SSIFqueue.front();
        m_SSIFqueue.pop();
        if (mvcpkt)
          return mvcpkt;
      }
    }
    // assume we are not eof
    if(m_pFormatContext->pb)
      m_pFormatContext->pb->eof_reached = 0;

    // check for saved packet after a program change
    if (m_pkt.result < 0)
    {
      // keep track if ffmpeg doesn't always set these
      m_pkt.pkt.size = 0;
      m_pkt.pkt.data = NULL;

      // timeout reads after 100ms
      m_timeout.Set(20000);
      m_pkt.result = av_read_frame(m_pFormatContext, &m_pkt.pkt);
      m_timeout.SetInfinite();
    }

    if (m_pkt.result == AVERROR(EINTR) || m_pkt.result == AVERROR(EAGAIN))
    {
      // timeout, probably no real error, return empty packet
      bReturnEmpty = true;
    }
    else if (m_pkt.result < 0)
    {
      Flush();
    }
    else if (!m_bSSIF && IsProgramChange())
    {
      // update streams
      CreateStreams(m_program);

      pPacket = CDVDDemuxUtils::AllocateDemuxPacket(0);
      pPacket->iStreamId = DMX_SPECIALID_STREAMCHANGE;

      return pPacket;
    }
    // check size and stream index for being in a valid range
    else if (m_pkt.pkt.size < 0 ||
             m_pkt.pkt.stream_index < 0 ||
             m_pkt.pkt.stream_index >= (int)m_pFormatContext->nb_streams)
    {
      // XXX, in some cases ffmpeg returns a negative packet size
      if(m_pFormatContext->pb && !m_pFormatContext->pb->eof_reached)
      {
        CLog::Log(LOGERROR, "CDVDDemuxFFmpeg::Read() no valid packet");
        bReturnEmpty = true;
        Flush();
      }
      else
        CLog::Log(LOGERROR, "CDVDDemuxFFmpeg::Read() returned invalid packet and eof reached");

      m_pkt.result = -1;
      av_free_packet(&m_pkt.pkt);
    }
    else
    {
      bool bDrop = false;
      
      ParsePacket(&m_pkt.pkt);

      AVStream *stream = m_pFormatContext->streams[m_pkt.pkt.stream_index];

      if (stream->codec->codec_type == AVMEDIA_TYPE_VIDEO && m_speed > DVD_PLAYSPEED_NORMAL) {
        if (m_pkt.pkt.flags & AV_PKT_FLAG_KEY)
          bDrop = false;
        else
          bDrop = true;
      }

      if (IsVideoReady() && !bDrop)
      {
        if (!m_bSSIF && m_program != UINT_MAX)
        {
          /* check so packet belongs to selected program */
          for (unsigned int i = 0; i < m_pFormatContext->programs[m_program]->nb_stream_indexes; i++)
          {
            if(m_pkt.pkt.stream_index == (int)m_pFormatContext->programs[m_program]->stream_index[i])
            {
              pPacket = CDVDDemuxUtils::AllocateDemuxPacket(m_pkt.pkt.size);
              break;
            }
          }

          if (!pPacket)
            bReturnEmpty = true;
        }
        else
          pPacket = CDVDDemuxUtils::AllocateDemuxPacket(m_pkt.pkt.size);
      }
      else
        bReturnEmpty = true;

      if (pPacket)
      {
        // lavf sometimes bugs out and gives 0 dts/pts instead of no dts/pts
        // since this could only happens on initial frame under normal
        // circomstances, let's assume it is wrong all the time
        if(m_pkt.pkt.dts == 0)
          m_pkt.pkt.dts = AV_NOPTS_VALUE;
        if(m_pkt.pkt.pts == 0)
          m_pkt.pkt.pts = AV_NOPTS_VALUE;

        if(m_bMatroska && stream->codec && stream->codec->codec_type == AVMEDIA_TYPE_VIDEO)
        { // matroska can store different timestamps
          // for different formats, for native stored
          // stuff it is pts, but for ms compatibility
          // tracks, it is really dts. sadly ffmpeg
          // sets these two timestamps equal all the
          // time, so we select it here instead
          if(stream->codec->codec_tag == 0)
            m_pkt.pkt.dts = AV_NOPTS_VALUE;
          else
            m_pkt.pkt.pts = AV_NOPTS_VALUE;
        }

        // we need to get duration slightly different for matroska embedded text subtitels
        if(m_bMatroska && stream->codec && stream->codec->codec_id == AV_CODEC_ID_TEXT && m_pkt.pkt.convergence_duration != 0)
          m_pkt.pkt.duration = m_pkt.pkt.convergence_duration;

        if(m_bAVI && stream->codec && stream->codec->codec_type == AVMEDIA_TYPE_VIDEO)
        {
          // AVI's always have borked pts, specially if m_pFormatContext->flags includes
          // AVFMT_FLAG_GENPTS so always use dts
          m_pkt.pkt.pts = AV_NOPTS_VALUE;
        }

        // copy contents into our own packet
        pPacket->iSize = m_pkt.pkt.size;

        // maybe we can avoid a memcpy here by detecting where pkt.destruct is pointing too?
        if (m_pkt.pkt.data)
          memcpy(pPacket->pData, m_pkt.pkt.data, pPacket->iSize);

        pPacket->pts = ConvertTimestamp(m_pkt.pkt.pts, stream->time_base.den, stream->time_base.num);
        pPacket->dts = ConvertTimestamp(m_pkt.pkt.dts, stream->time_base.den, stream->time_base.num);
        pPacket->duration =  DVD_SEC_TO_TIME((double)m_pkt.pkt.duration * stream->time_base.num / stream->time_base.den);

        // used to guess streamlength
        if (pPacket->dts != DVD_NOPTS_VALUE && (pPacket->dts > m_currentPts || m_currentPts == DVD_NOPTS_VALUE))
          m_currentPts = pPacket->dts;


        // check if stream has passed full duration, needed for live streams
        bool bAllowDurationExt = (stream->codec && (stream->codec->codec_type == AVMEDIA_TYPE_VIDEO || stream->codec->codec_type == AVMEDIA_TYPE_AUDIO));
        if(bAllowDurationExt && m_pkt.pkt.dts != (int64_t)AV_NOPTS_VALUE)
        {
          int64_t duration;
          duration = m_pkt.pkt.dts;
          if(stream->start_time != (int64_t)AV_NOPTS_VALUE)
            duration -= stream->start_time;

          if(duration > stream->duration)
          {
            stream->duration = duration;
            duration = av_rescale_rnd(stream->duration, (int64_t)stream->time_base.num * AV_TIME_BASE, stream->time_base.den, AV_ROUND_NEAR_INF);
            if ((m_pFormatContext->duration == (int64_t)AV_NOPTS_VALUE)
                ||  (m_pFormatContext->duration != (int64_t)AV_NOPTS_VALUE && duration > m_pFormatContext->duration))
              m_pFormatContext->duration = duration;
          }
        }

        // store internal id until we know the continuous id presented to player
        // the stream might not have been created yet
        pPacket->iStreamId = m_pkt.pkt.stream_index;
      }
      m_pkt.result = -1;
      av_free_packet(&m_pkt.pkt);
    }
  }
  } // end of lock scope
  if (bReturnEmpty && !pPacket)
    pPacket = CDVDDemuxUtils::AllocateDemuxPacket(0);

  if (!pPacket) return NULL;

  // check streams, can we make this a bit more simple?
  if (pPacket && pPacket->iStreamId >= 0)
  {
    CDemuxStream *stream = GetStreamInternal(pPacket->iStreamId);
    if (!stream ||
        stream->pPrivate != m_pFormatContext->streams[pPacket->iStreamId] ||
        stream->codec != m_pFormatContext->streams[pPacket->iStreamId]->codec->codec_id)
    {
      // content has changed, or stream did not yet exist
      stream = AddStream(pPacket->iStreamId);
    }
    // we already check for a valid m_streams[pPacket->iStreamId] above
    else if (stream->type == STREAM_AUDIO)
    {
      if (((CDemuxStreamAudio*)stream)->iChannels != m_pFormatContext->streams[pPacket->iStreamId]->codec->channels ||
          ((CDemuxStreamAudio*)stream)->iSampleRate != m_pFormatContext->streams[pPacket->iStreamId]->codec->sample_rate)
      {
        // content has changed
        stream = AddStream(pPacket->iStreamId);
      }
    }
    else if (stream->type == STREAM_VIDEO)
    {
      if (((CDemuxStreamVideo*)stream)->iWidth != m_pFormatContext->streams[pPacket->iStreamId]->codec->width ||
          ((CDemuxStreamVideo*)stream)->iHeight != m_pFormatContext->streams[pPacket->iStreamId]->codec->height)
      {
        // content has changed
        stream = AddStream(pPacket->iStreamId);
      }
      if (m_bSSIF && stream->iPhysicalId == 0x1011)
      {
        if (m_SSIFqueue.size() <= 0)    
          CLog::Log(LOGERROR, "!!! MVC error: no mvc packet: pts(%f) dts(%f) - %lld", pPacket->pts, pPacket->dts, m_pkt.pkt.pts); 
        else  
        {    
          DemuxPacket* mvcpkt = m_SSIFqueue.front(); 
          double tsA = (pPacket->dts != AV_NOPTS_VALUE ? pPacket->dts : pPacket->pts); 
          double tsB = (mvcpkt->dts != AV_NOPTS_VALUE ? mvcpkt->dts : mvcpkt->pts);  
          while (tsB < tsA)  
          {      
            m_SSIFqueue.pop();   
            if (m_SSIFqueue.empty())    
            {      
              tsB = AV_NOPTS_VALUE;
              break;
            }
            CDVDDemuxUtils::FreeDemuxPacket(mvcpkt);
            mvcpkt = m_SSIFqueue.front();
            tsB = (mvcpkt->dts != AV_NOPTS_VALUE ? mvcpkt->dts : mvcpkt->pts);
          }
          if (tsA == tsB)
          {
            mvcpkt->pts = (double)((int64_t)pPacket->pts + 5);
            mvcpkt->iStreamId = pPacket->iStreamId;
            m_bNeedMVC = true;
          }
          else
            CLog::Log(LOGERROR, "!!! MVC error: missing mvc packet: pts(%f) dts(%f) - %lld tsA - %f tsB - %f", pPacket->pts, pPacket->dts, m_pkt.pkt.pts, tsA, tsB);
        }
      }
    }
    else if (stream->type == STREAM_DATA)
    {
      if (m_bSSIF && stream->iPhysicalId == 0x1012)
      { 
        DemuxPacket* newpkt = CDVDDemuxUtils::AllocateDemuxPacket(pPacket->iSize); 
        newpkt->iSize = pPacket->iSize;  
        newpkt->pts = pPacket->pts; 
        newpkt->dts = pPacket->dts; 
        newpkt->duration = pPacket->duration; 
        newpkt->iGroupId = pPacket->iGroupId;  
        newpkt->iStreamId = pPacket->iStreamId;
        memcpy(newpkt->pData, pPacket->pData, newpkt->iSize); 
        m_SSIFqueue.push(newpkt); 
        CDVDDemuxUtils::FreeDemuxPacket(pPacket); 
        pPacket = CDVDDemuxUtils::AllocateDemuxPacket(0);
        pPacket->iSize = 0;
      }
    }
    if (!stream)
    {
      CLog::Log(LOGERROR, "CDVDDemuxFFmpeg::AddStream - internal error, stream is null");
      CDVDDemuxUtils::FreeDemuxPacket(pPacket);
      return NULL;
    }
    // set continuous stream id for player
    pPacket->iStreamId = stream->iId;
  }
  return pPacket;
}


