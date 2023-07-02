/* -*- Mode:C++; c-file-style:"gnu"; indent-tabs-mode:nil; -*- */
/*
 * Copyright (c) 2016 ResiliNets, ITTC, University of Kansas
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * Author: Truc Anh N. Nguyen <annguyen@ittc.ku.edu>
 *
 * James P.G. Sterbenz <jpgs@ittc.ku.edu>, director
 * ResiliNets Research Group  http://wiki.ittc.ku.edu/resilinets
 * Information and Telecommunication Technology Center (ITTC)
 * and Department of Electrical Engineering and Computer Science
 * The University of Kansas Lawrence, KS USA.
 */

#include "tcp-cerl.h"
#include "tcp-socket-state.h"
#include "tcp-socket-base.h"
#include "ns3/log.h"

namespace ns3 {

NS_LOG_COMPONENT_DEFINE ("TcpCerl");
NS_OBJECT_ENSURE_REGISTERED (TcpCerl);

TypeId
TcpCerl::GetTypeId (void)
{
  static TypeId tid = TypeId ("ns3::TcpCerl")
    .SetParent<TcpNewReno> ()
    .AddConstructor<TcpCerl> ()
    .SetGroupName ("Internet")
    .AddAttribute ("dqlt", "Threshold for congestion detection",
                   UintegerValue (0),
                   MakeUintegerAccessor (&TcpCerl::m_dqlt),
                   MakeUintegerChecker<uint32_t> ())
  ;
  return tid;
}

TcpCerl::TcpCerl (void)
  : TcpNewReno (),
    m_baseRtt (Time::Max ()),
    m_minRtt (Time::Max ()),
    m_cntRtt (0),
    m_doingCerlNow (true),
    m_qlength (0),
    m_inc (true),
    m_ackCnt (0),
    m_dqlt (0)
{
  NS_LOG_FUNCTION (this);
}

TcpCerl::TcpCerl (const TcpCerl& sock)
  : TcpNewReno (sock),
    m_baseRtt (sock.m_baseRtt),
    m_minRtt (sock.m_minRtt),
    m_cntRtt (sock.m_cntRtt),
    m_doingCerlNow (true),
    m_qlength (0),
    m_qlengthMax(0), //---added---
    m_inc (true),
    m_ackCnt (sock.m_ackCnt),
    m_dqlt (0) , //---added---
    m_maxSentSeqno (0),
    m_highestAckSent (0)
{
  NS_LOG_FUNCTION (this);
}

TcpCerl::~TcpCerl (void)
{
  NS_LOG_FUNCTION (this);
}

Ptr<TcpCongestionOps>
TcpCerl::Fork (void)
{
  return CopyObject<TcpCerl> (this);
}

void
TcpCerl::PktsAcked (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked,
                    const Time& rtt)
{
  NS_LOG_FUNCTION (this << tcb << segmentsAcked << rtt);

  if (rtt.IsZero ())
    {
      return;
    }

  m_minRtt = std::min (m_minRtt, rtt);
  NS_LOG_DEBUG ("Updated m_minRtt= " << m_minRtt);


  m_baseRtt = std::min (m_baseRtt, rtt);
  NS_LOG_DEBUG ("Updated m_baseRtt= " << m_baseRtt);

  // Update RTT counter
  m_cntRtt++;
  NS_LOG_DEBUG ("Updated m_cntRtt= " << m_cntRtt);
}

void
TcpCerl::EnableCerl ()
{
  NS_LOG_FUNCTION (this);

  m_doingCerlNow = true;
  m_minRtt = Time::Max ();
}

void
TcpCerl::DisableCerl ()
{
  NS_LOG_FUNCTION (this);

  m_doingCerlNow = false;
}

void
TcpCerl::CongestionStateSet (Ptr<TcpSocketState> tcb,
                             const TcpSocketState::TcpCongState_t newState)
{
  NS_LOG_FUNCTION (this << tcb << newState);
  if (newState == TcpSocketState::CA_OPEN)
    {
      EnableCerl ();
      NS_LOG_LOGIC ("Cerl is now on.");
    }
  else
    {
      DisableCerl ();
      NS_LOG_LOGIC ("Cerl is turned off.");
    }
}

void
TcpCerl::IncreaseWindow (Ptr<TcpSocketState> tcb, uint32_t segmentsAcked)
{
  NS_LOG_FUNCTION (this << tcb << segmentsAcked);

  // Always calculate m_qlength, even if we are not doing Cerl now
  //----calculating bottleneck queue length----
  uint32_t targetCwnd;
  uint32_t segCwnd = tcb->GetCwndInSegments ();
  double tmp = m_baseRtt.GetSeconds () / m_minRtt.GetSeconds ();
  targetCwnd = static_cast<uint32_t> (segCwnd * tmp);

  NS_ASSERT (segCwnd >= targetCwnd); // implies baseRtt <= minRtt
  m_qlength = segCwnd - targetCwnd;
  NS_LOG_DEBUG ("Calculated m_qlength(L) = " << m_qlength);
  
  //----calculating dynamic queue length threshold----
  if(m_qlength>m_qlengthMax){
     m_qlengthMax=m_qlength;
  }
  m_dqlt=0.55*m_qlengthMax;    
  
  NS_LOG_DEBUG ("Calculated m_bqlt(N) = " << m_dqlt);
 
  //--------------------------------------------------

  if (!m_doingCerlNow)
    {
      // If Cerl is not on, we follow NewReno algorithm
      NS_LOG_LOGIC ("Cerl is not turned on, we follow NewReno algorithm.");
      TcpNewReno::IncreaseWindow (tcb, segmentsAcked);
      return;
    }

  // We do the Cerl calculations only if we got enough RTT samples
  if (m_cntRtt <= 2)//----------confusion !!!
    {    // We do not have enough RTT samples, so we should behave like NewReno
      NS_LOG_LOGIC ("We do not have enough RTT samples to perform Cerl "
                    "calculations, we behave like NewReno.");
      TcpNewReno::IncreaseWindow (tcb, segmentsAcked);
    }
  else
    {
      NS_LOG_LOGIC ("We have enough RTT samples to perform Cerl calculations.");

      if (tcb->m_cWnd < tcb->m_ssThresh)
        { // Slow start mode. Cerl employs same slow start algorithm as NewReno's.
          NS_LOG_LOGIC ("We are in slow start, behave like NewReno.");
          TcpNewReno::SlowStart (tcb, segmentsAcked);
        }
      else
        { // Congestion avoidance mode
          NS_LOG_LOGIC ("We are in congestion avoidance, behave like NewReno.");
          TcpNewReno::CongestionAvoidance (tcb, segmentsAcked);
        }
    }

  // Reset cntRtt & minRtt every RTT
  m_cntRtt = 0;
  m_minRtt = Time::Max ();
}

std::string
TcpCerl::GetName () const
{
  return "TcpCerl";
}

uint32_t
TcpCerl::GetSsThresh (Ptr<const TcpSocketState> tcb,
                      uint32_t bytesInFlight)
{
  NS_LOG_FUNCTION (this << tcb << bytesInFlight);
  
  if(m_highestAckSent < tcb->m_highTxAck){
    m_highestAckSent=tcb->m_highTxAck;
  }
  
  if (m_qlength >= m_dqlt && m_highestAckSent-1>m_maxSentSeqno)
    {
      // congestion-based loss is most likely to have occurred,
      // we reduce cwnd by 1/2 as in NewReno
      NS_LOG_LOGIC ("Congestive loss is most likely to have occurred, "
                    "cwnd is halved");
      m_maxSentSeqno=tcb->m_highTxMark;               
      return TcpNewReno::GetSsThresh (tcb, bytesInFlight);
    }
  else
    {
      // random loss due to bit errors is most likely to have occurred,
      NS_LOG_LOGIC ("Random loss is most likely to have occurred");
      tcb->m_oldcWnd=bytesInFlight;
      return std::max (static_cast<uint32_t> (bytesInFlight),
                       2 * tcb->m_segmentSize); //---no change in cwnd and ssthresh
                       
    }
}

} // namespace ns3
