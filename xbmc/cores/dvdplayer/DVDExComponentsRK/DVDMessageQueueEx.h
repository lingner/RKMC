#pragma once

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

#include "../DVDMessage.h"
#include "../DVDMessageQueue.h"
#include <string>
#include <list>
#include <algorithm>
#include "threads/CriticalSection.h"
#include "threads/Event.h"

#define DVDMESSAGE_LOCK_PRIORITY 10

class CDVDMessageQueueEx
{
public:
  CDVDMessageQueueEx(const std::string &owner);
  virtual ~CDVDMessageQueueEx();

  void  Init();
  void  Flush(CDVDMsg::Message message = CDVDMsg::DEMUXER_PACKET);
  void  Abort();
  void  End();

  MsgQueueReturnCode Put(CDVDMsg* pMsg, int priority = 0);

  /**
   * msg,       message type from DVDMessage.h
   * timeout,   timeout in msec
   * priority,  minimum priority to get, outputs returned packets priority
   */
  MsgQueueReturnCode Get(CDVDMsg** pMsg, unsigned int iTimeoutInMilliSeconds, int &priority);
  MsgQueueReturnCode Get(CDVDMsg** pMsg, unsigned int iTimeoutInMilliSeconds)
  {
    int priority = 0;
    return Get(pMsg, iTimeoutInMilliSeconds, priority);
  }

  MsgQueueReturnCode LockFirst(CDVDMsg** pMsg, unsigned int iTimeoutInMilliSeconds, int &priority);
  MsgQueueReturnCode Pop();

  int GetDataSize() const               { return m_iDataSize; }
  int GetTimeSize() const;
  unsigned GetPacketCount(CDVDMsg::Message type);
  bool ReceivedAbortRequest()           { return m_bAbortRequest; }
  void WaitUntilEmpty();

  // non messagequeue related functions
  bool IsFull() const                   { return GetLevel() == 100; }
  int  GetLevel() const;

  void SetMaxDataSize(int iMaxDataSize) { m_iMaxDataSize = iMaxDataSize; }
  void SetMaxTimeSize(double sec)       { m_TimeSize  = 1.0 / std::max(1.0, sec); }
  int GetMaxDataSize() const            { return m_iMaxDataSize; }
  double GetMaxTimeSize() const         { return m_TimeSize; }
  bool IsInited() const                 { return m_bInitialized; }
  bool IsDataBased() const;

private:

  CEvent m_hEvent;
  mutable CCriticalSection m_section;

  bool m_bAbortRequest;
  bool m_bInitialized;
  bool m_bCaching;

  int m_iDataSize;
  double m_TimeFront;
  double m_TimeBack;
  double m_TimeSize;

  int m_iMaxDataSize;
  bool m_bEmptied;
  std::string m_owner;

  typedef std::list<DVDMessageListItem> SList;
  SList m_list;
};

