/* -*-  Mode: C++; c-file-style: "gnu"; indent-tabs-mode:nil; -*- */
/*
 * Author: Sérgio Vieira
 * MACC - Mestrado Acadêmico em Ciência da Computação - 2012
 */

#include <cmath>
#include <algorithm>
#include <limits>
#include <sstream>
#include "ns3/log.h"
#include "ns3/uinteger.h"
#include "ns3/address.h"
#include "ns3/mac48-address.h"
#include "ns3/mobility-model.h"
#include "ns3/trace-source-accessor.h"
#include "ns3/nstime.h"
#include "ns3/boolean.h"
#include "bp-rt-prophet.h"
#include "bp-header.h"
#include "bp-type-tag.h"
#include "bp-contact.h"
#include "bp-link-manager.h"
#include "bp-neighbourhood-detection-agent.h"

#define PINIT 0.5
#define GAMA 0.98
#define BETA 0.25

NS_LOG_COMPONENT_DEFINE ("RTProphet");

namespace ns3 {
namespace bundleProtocol {

NS_OBJECT_ENSURE_REGISTERED (RTProphet);

TypeId RTProphet::GetTypeId(void) {
	static TypeId tid = TypeId ("ns3::bundleProtocol::RTProphet")
		.SetParent<BundleRouter> ()
		.AddConstructor<RTProphet> ()
		.AddAttribute ("AlwaysSendHello",
				"Sets if the router always should send hellos or only when it have something to send.",
				BooleanValue (true),
				MakeBooleanAccessor (&RTProphet::m_alwaysSendHello),
				MakeBooleanChecker ())
		.AddTraceSource ("RedundantRelay", "A message already held in the buffer has been received.",
				MakeTraceSourceAccessor (&RTProphet::m_redundantRelayLogger));

	return tid;
}

RTProphet::RTProphet() : BundleRouter()
{
}

RTProphet::~RTProphet()
{
}

/* sergiosvieira */
void RTProphet::ProbabilityExpirationTimer(Ptr<Link> link) {
	//link->SetProbability(link->GetProbability() * GAMA);
	ProbabilityReduce();
	//NS_LOG_DEBUG("(" << m_node->GetId() << ") - Link Probability: " << link->GetProbability() << " With " << link->GetRemoteEndpointId());
	Simulator::Schedule (Seconds(10.0), &RTProphet::ProbabilityExpirationTimer, this, link);
}
/* sergiosvieira */

void RTProphet::DoInit()
{
	if (m_alwaysSendHello) {
		m_nda->Start();
	}
}

void RTProphet::DoDispose()
{
	BundleRouter::DoDispose();
}

void RTProphet::DoLinkClosed(Ptr<Link> link)
{
	if (link->GetContact() != 0) {
		BundleList bundles = link->GetContact()->GetQueuedBundles();
		for (BundleList::iterator iter = bundles.begin(); iter != bundles.end(); ++iter) {
			link->GetContact()->DequeueBundle(*iter);
			CancelTransmission(*iter, link);
		}
	}

	RemoveRouterSpecificBundles(link);
}

void RTProphet::RemoveRouterSpecificBundles(Ptr<Link> link)
{
}

void RTProphet::PauseLink(Ptr<Link> link)
{
	if (link->GetState() == LINK_CONNECTED) {
		BundleList bundles = link->GetContact()->GetQueuedBundles();

		for (BundleList::iterator iter = bundles.begin(); iter != bundles.end(); ++iter) {
			link->GetContact()->DequeueBundle(*iter);
			CancelTransmission(*iter, link);
		}

		link->ChangeState(LINK_PAUSED);

		Ptr<Contact> c = link->GetContact();

		if (c->GetRetransmissions() >= m_maxRetries) {
			m_linkManager->CloseLink(link);
		} else {
			c->IncreaseRetransmissions();
			Simulator::Schedule(m_pauseTime, &RTProphet::UnPauseLink, this, link);
		}
	}
	Simulator::ScheduleNow(&RTProphet::TryToStartSending, this);
}

void RTProphet::UnPauseLink(Ptr<Link> link)
{
	if (link->GetState() == LINK_PAUSED) {
		link->ChangeState(LINK_CONNECTED);
		Simulator::ScheduleNow(&RTProphet::TryToStartSending, this);
	}
}

void RTProphet::DoLinkDiscovered(Ptr<Link> link)
{
	/* sergiosvieira */
	float p = link->GetProbability();
	link->SetProbability(p + (1.0 - p) * PINIT);
	NS_LOG_DEBUG("(" << m_node->GetId()<<") - New Contact - Link Probability = " << link->GetProbability() <<" With " << link->GetRemoteEndpointId());
	/* sergiosvieira */
	m_linkManager->OpenLink(link);
	Simulator::ScheduleNow(&RTProphet::TryToStartSending, this);
	Simulator::ScheduleNow(&RTProphet::ProbabilityExpirationTimer, this, link);
}

void RTProphet::DoBundleReceived(Ptr<Bundle> bundle)
{
        NS_LOG_DEBUG("");
        SetBundleReceived("RTProphet.out",bundle);
	/***Joao***/
	/*Aqui seta 1, pois o bundle enviado tem valor 0, que não permiti que esse bundle seja reenviado*/
	//bundle->SetReplicationFactor(1);
	/*Joao*****/
	Ptr<Link> link = m_linkManager->FindLink(
			bundle->GetReceivedFrom().front().GetEndpoint());
	if (link != 0) {
		link->UpdateLastHeardFrom();
	}
}

Ptr<Bundle> RTProphet::DoSendBundle(Ptr<Link> link, Ptr<Bundle> bundle)
{
	NS_LOG_DEBUG("("<<m_node->GetId()<<")" << " To " <<bundle->GetDestinationEndpoint());
	if(link->GetContact())
	{
		link->GetContact()->EnqueueBundle(bundle);
	}
	/*joao*/
	/**Quando eu for enviar um bundle set esse fator como sendo 0, assim no próxima tentativa de enviar no trystar
	  tosend ele irá ver que esse valor é 0 e não irá enviar o bundle*/
	//bundle->SetReplicationFactor(0);
	/*joao*/
	Ptr<Bundle> send = bundle->Copy();
	//send->SetLifetime(200);
	return send;
}

void RTProphet::DoBundleSent(const Address& address,
		const GlobalBundleIdentifier& gbid, bool finalDelivery)
{
	Mac48Address mac = Mac48Address::ConvertFrom(address);
	Ptr<Link> link = m_linkManager->FindLink(mac);


	NS_LOG_DEBUG("("<<m_node->GetId()<<")");

	Ptr<Bundle> bundle = GetBundle(gbid);
	if (bundle != 0) {
		// I have received an ack for the sent bundle, so i have heard from the
		// other node.
		link->UpdateLastHeardFrom();

		if (link->GetState() == LINK_CONNECTED || link->GetState()
				== LINK_PAUSED) {
			link->GetContact()->DequeueBundle(gbid);
			link->GetContact()->ResetRetransmissions();
		}

		m_forwardLog.AddEntry(bundle, link);

		if (finalDelivery) {
			BundleDelivered(bundle, true);
		}

		Simulator::ScheduleNow(&RTProphet::TryToStartSending, this);
	} else {
		if (finalDelivery) {
			NS_LOG_DEBUG("("<<m_node->GetId()<<")" <<" Final Delivery ");
			// This is a ugly hack utilitzing the fact that i know that
			// the ttl is "inifinite".
			Ptr<Bundle> bundle = Create<Bundle> ();
			bundle->SetSourceEndpoint(gbid.GetSourceEid());
			bundle->SetCreationTimestamp(gbid.GetCreationTimestamp());
			bundle->SetLifetime(43000);
			BundleDelivered(bundle, true);

			Simulator::ScheduleNow(&RTProphet::TryToStartSending, this);
		}
	}
}

void RTProphet::DoBundleTransmissionFailed(const Address& address,
		const GlobalBundleIdentifier& gbid)
{
	Mac48Address mac = Mac48Address::ConvertFrom(address);
	Ptr<Link> link = m_linkManager->FindLink(mac);
	PauseLink(link);
}

bool RTProphet::DoAcceptBundle(Ptr<Bundle> bundle,
		bool fromApplication)
{
	if (HasBundle(bundle)) {
		m_redundantRelayLogger(bundle);
		Ptr<Bundle> otherBundle = GetBundle(bundle->GetBundleId());
		otherBundle->AddReceivedFrom(bundle->GetReceivedFrom().front());
		return false;
	}
	return CanMakeRoomForBundle(bundle);
}

bool RTProphet::DoCanDeleteBundle(const GlobalBundleIdentifier& gbid)
{
	return true;
}

void RTProphet::DoInsert(Ptr<Bundle> bundle)
{
	NS_LOG_DEBUG("");
	// This is my extra thing, i always set the eid to current node holding the bundle.
	bundle->SetCustodianEndpoint(m_eid);
	//bundle->SetCustodyTransferRequested(true);

	m_bundleList.push_back(bundle);

	// If this is the first bundle, I now want to begin sending hello messages announcing that
	// I have something to send. If there is more than one bundle in the queue this means that
	// I already have started sending hello messages.
	if (m_nBundles == 1 && !m_alwaysSendHello) {
		m_nda->Start();
	}

	Simulator::ScheduleNow(&RTProphet::TryToStartSending, this);
}

bool RTProphet::CanMakeRoomForBundle(Ptr<Bundle> bundle)
{
	if (bundle->GetSize() < m_maxBytes) {
		return true;
	} else {
		return false;
	}

}

bool RTProphet::MakeRoomForBundle(Ptr<Bundle> bundle)
{
	if (bundle->GetSize() < m_maxBytes) {
		if (bundle->GetSize() < GetFreeBytes()) {
			return true;
		}

		    //for (BundleList::reverse_iterator iter = m_bundleList.rbegin(); iter
                               // != m_bundleList.rend();) {
				/*Politica atual: remove o elemento mais velho da fila para colocar o que chegou*/
                for (BundleList::iterator iter = m_bundleList.begin(); iter
                                                != m_bundleList.end();) {
                        Ptr<Bundle> currentBundle = *(iter++);

                        DeleteBundle(currentBundle, true);
                        if (bundle->GetSize() < GetFreeBytes()) {
                                return true;
                        }
                }
	}
	SetBufferOverFlow("RTProphet.buff");
	return false;
}

bool RTProphet::DoDelete(const GlobalBundleIdentifier& gbid,
		bool drop)
{
	NS_LOG_DEBUG("("<<m_node->GetId()<<")");
	// If this is the last bundle in the queue, stop sending Hello messages.
	if (m_nBundles == 1 && !m_alwaysSendHello) {
		//m_nda->Stop();
	}
	if (drop)/*drop = true indica que o bundle expirou por isso está sendo apagado*/
	{
               SetBundleExpired("RTProphet.expired");
	}
	return BundleRouter::DoDelete(gbid, drop);
}

void RTProphet::DoCancelTransmission(Ptr<Bundle> bundle,
		Ptr<Link> link)
{
}

void RTProphet::DoTransmissionCancelled(const Address& address,
		const GlobalBundleIdentifier& gbid)
{
	Mac48Address mac = Mac48Address::ConvertFrom(address);
	Ptr<Link> link = m_linkManager->FindLink(mac);
	if(link->GetContact())
	{
            link->GetContact()->DequeueBundle(gbid);
	}

	Simulator::ScheduleNow(&RTProphet::TryToStartSending, this);
}

void RTProphet::TryToStartSending()
{
	NS_LOG_DEBUG("(" << m_node->GetId () << ")");
	RemoveExpiredBundles(true);
	m_forwardLog.RemoveExpiredEntries();

	if (!IsSending() && (GetNBundles() > 0)) {
		LinkBundle linkBundle = FindNextToSend();
		if (!linkBundle.IsNull()) {
                        NS_LOG_DEBUG("("<<m_node->GetId() <<") Send to Suc");
            linkBundle.GetBundle()->SetCustodyTransferRequested(true);
			//SendBundle(linkBundle.GetLink(), linkBundle.GetBundle());
			NS_LOG_DEBUG("(" << m_node->GetId () << ")" <<" Enviar" );
			Ptr<MobilityModel> mm = m_node->GetObject<MobilityModel> ();
			//std::cout<<"Node ID:("<<m_node->GetId ()<<") Send Bundle ID: *"<<linkBundle.GetBundle()->GetGlobalId()<<"* To Node ("<<linkBundle.GetLink()->GetRemoteEndpointId()<<") ";
			//std::cout<<"Time: "<< Simulator::Now().GetSeconds()<<" Pos = "<<mm->GetPosition ()<<"\n";
			SendBundle(linkBundle.GetLink(), linkBundle.GetBundle());

			/*Inseri na lista de pacotes que trasnferiram custodia, serve para que ele nao receba novamente o mesmo pacote*/
			InsertCustodyHistorical(linkBundle.GetBundle()->GetBundleId());
			/*Inseri na lista de pacotes que trasnferiram custodia, mas ainda não esperaram resposta*/
			InsertCustodyHistoricalPending(linkBundle.GetBundle()->GetBundleId());
			Simulator::Schedule (Seconds(5.0), &RTProphet::EraseCustodyHistoricalPending,this,linkBundle.GetBundle()->GetBundleId());
		} else {
			Simulator::Schedule (Seconds(1.0), &RTProphet::TryToStartSending, this);
		}
	}
}

LinkBundle RTProphet::GetBestLink(LinkBundleList lbl) {

	//PrintTable();
	LinkBundle lb = lbl.front();
	Ptr<Link> link = lb.GetLink();

	BundleEndpointId remote = link->GetRemoteEndpointId();

	NS_LOG_DEBUG("\t Link Source = " << m_eid << " Destination = " <<  remote );

	if (lb.GetBundle()->GetDestinationEndpoint() == remote) {
		NS_LOG_DEBUG("\t Direct Link");
		return LinkBundle(link, lb.GetBundle());
	}

	double max = 0;//m_prob_table[remote].probability;
	BundleEndpointId max_eid = m_node->GetId();//remote;

	for (LinkBundleList::iterator it = lbl.begin(); it != lbl.end(); ++it) {

		if (lb.GetBundle()->GetDestinationEndpoint() == (*it).GetLink()->GetRemoteEndpointId()) {
			NS_LOG_DEBUG("\t Direct Link 2");
			return LinkBundle((*it).GetLink(), lb.GetBundle());
		}

		//prop_table[(*it).GetLink()->GetRemoteEndpointId()].prob; /*Lista de Probabilidades do nó remoto*/

		ProbabilitiesList::iterator itPb = m_prob_table[(*it).GetLink()->GetRemoteEndpointId()].prob.find(lb.GetBundle()->GetDestinationEndpoint());

		if(itPb != m_prob_table[(*it).GetLink()->GetRemoteEndpointId()].prob.end())
		{


			double p = (*itPb).second;//m_prob_table[(*it).GetLink()->GetRemoteEndpointId()].probability;
			NS_LOG_DEBUG("Remote Probability: " <<"(" <<(*it).GetLink()->GetRemoteEndpointId() <<") "<<p);
			if (p > max) {
				max = p;
				max_eid = (*it).GetLink()->GetRemoteEndpointId();
			}
		}
	}
	NS_LOG_DEBUG("\t Indirect Link");
	if(max == 0){

		NS_LOG_DEBUG("\t No Send");
		return LinkBundle(0,0);
	}
	//PrintTable();
	NS_LOG_DEBUG("("<<m_node->GetId() <<") \t Try Send to (" <<max_eid <<")");
	return LinkBundle(m_linkManager->FindLink(max_eid), lb.GetBundle());
}

LinkBundle RTProphet::FindNextToSend()
{
	//NS_LOG_DEBUG("RTProphet::FindNextToSend");
	NS_LOG_DEBUG("(" << m_node->GetId() << ") - m_linkManager->GetConnectedLinks().size()= " << m_linkManager->GetConnectedLinks().size() << " GetNBundles() = " << GetNBundles() );
	if ((m_linkManager->GetConnectedLinks().size() > 0) && (GetNBundles() > 0)) {
		LinkBundleList linkBundleList = GetAllDeliverableBundles();
		if (!linkBundleList.empty()) {
			NS_LOG_DEBUG("\t Not Empty!!");

			return GetBestLink(linkBundleList);

			//return linkBundleList.front();
		} else {
			NS_LOG_DEBUG("\t Empty!!");
		}
	}
	return LinkBundle(0, 0);
}

LinkBundle RTProphet::GetNextRouterSpecific()
{
	return LinkBundle(0, 0);
}

LinkBundleList RTProphet::GetAllDeliverableBundles()
{
	Links links = m_linkManager->GetConnectedLinks();
	LinkBundleList result;
	for (Links::iterator iter = links.begin(); iter != links.end(); ++iter) {
		Ptr<Link> link = *iter;
		/* sergiosvieira */
		//LinkBundleList linkBundleList = GetAllBundlesForLink(link);
		LinkBundleList linkBundleList = GetAllBundlesToAllLinks();
		/* sergiosvieira */
		result.insert(result.end(), linkBundleList.begin(),
				linkBundleList.end());
	}
	return result;
}

LinkBundleList RTProphet::GetAllBundlesForLink(Ptr<Link> link)
{
	LinkBundleList linkBundleList;
		LinkBundleList direct;
		if (link->GetState() == LINK_CONNECTED) {
			if (link->GetState() == LINK_CONNECTED) {
				for (BundleList::iterator iter = m_bundleList.begin(); iter
						!= m_bundleList.end(); ++iter) {
					Ptr<Bundle> bundle = *iter;
					/*
					if (bundle->HasRetentionConstraint(AddRetentionConstraint(RC_CUSTODY_ACCEPTED))
							&& !bundle->HaveBeenReceivedFrom(link)
							&& (link->GetRemoteEndpointId()
									== bundle->GetDestinationEndpoint())
							&& !m_forwardLog.HasEntry(bundle, link)) {
						linkBundleList.push_back(LinkBundle(link, *iter));
					}
					*/
					 if(!bundle->HaveBeenReceivedFrom(link->GetRemoteEndpointId().GetId()) && link->GetRemoteEndpointId() == bundle->GetDestinationEndpoint()){
						direct.push_back(LinkBundle(link,*iter));
						return direct;
					}
					/* agora não tem mais restrição de EID - virou epidêmico */
					if (!bundle->HaveBeenReceivedFrom(link)
							&& !m_forwardLog.HasEntry(bundle, link)
							&& !isBundleCustodyPending(bundle->GetBundleId())) {
						linkBundleList.push_back(LinkBundle(link, *iter));
					}

				}
			}
		}
		return linkBundleList;
}

LinkBundleList RTProphet::GetAllBundlesToAllLinks() {
	Links links = m_linkManager->GetConnectedLinks();

	LinkBundleList linkBundleList;
	for (Links::iterator iter = links.begin(); iter != links.end(); ++iter) {
		Ptr<Link> link = *iter;
		for (BundleList::iterator it = m_bundleList.begin(); it
				!= m_bundleList.end(); ++it) {
			Ptr<Bundle> bundle = *it;
			if (bundle->HasRetentionConstraint(RC_FORWARDING_PENDING)
					&& !bundle->HaveBeenReceivedFrom(link)
					&& !m_forwardLog.HasEntry(bundle, link)) {
				linkBundleList.push_back(LinkBundle(link, bundle));
			}
		}
	}
	return linkBundleList;
}


uint8_t RTProphet::DoCalculateReplicationFactor(
		const BundlePriority& priority) const
{
	return 1;
}

bool RTProphet::DoIsRouterSpecific(Ptr<Bundle> bundle)
{
	return false;
}

bool RTProphet::DoIsRouterSpecific(const BlockType& block)
{
	return false;
}

void RTProphet::SendRouterSpecific(Ptr<Link> link,
		Ptr<Bundle> bundle)
{
}

void RTProphet::SentRouterSpecific(Ptr<Link> link,
		const GlobalBundleIdentifier& gbid)
{
}

void RTProphet::ReceiveRouterSpecific(Ptr<Bundle> bundle)
{
}

void RTProphet::AddRouterSpecificBundle(Ptr<Bundle> bundle)
{
}

void RTProphet::RemoveRouterSpecificBundle(
		const GlobalBundleIdentifier& gbid, uint8_t reason)
{
}

bool RTProphet::HasRouterSpecificBundle(
		const GlobalBundleIdentifier& gbid)
{
	return false;
}

Ptr<Bundle> RTProphet::GetRouterSpecificBundle(
		const GlobalBundleIdentifier& gbid)
{
	return Create<Bundle> ();
}

void RTProphet::DoBundleDelivered(Ptr<Bundle> bundle, bool fromAck) {
	NS_LOG_DEBUG("(" << m_node->GetId() << ") - From = " << bundle->GetCustodianEndpoint() << " - fromAck = " << fromAck);

	m_kdm.Insert(bundle);

	BundleList bl;
	for (BundleList::iterator iter = m_bundleList.begin(); iter != m_bundleList.end(); ++iter) {
		NS_LOG_DEBUG("--> BUNDLE EID: " << (*iter)->GetCustodianEndpoint());
		Ptr<Bundle> bundle = *iter;
		if (m_kdm.Has(bundle)) {
			bl.push_back(bundle);
			m_forwardLog.RemoveEntriesFor(bundle->GetBundleId());
		}
	}

	for (BundleList::iterator iter = bl.begin(); iter != bl.end(); ++iter) {
		NS_LOG_DEBUG("--> Delivered EID: " << (*iter)->GetBundleId());
		DeleteBundle(*iter, false);
	}
}

Ptr<Link> RTProphet::DoCreateLink(const BundleEndpointId& eid, const Address& address)
{
	NS_LOG_DEBUG ("(" << m_node->GetId () << ") ");
	Ptr<ConvergenceLayerAgent> cla = m_node->GetObject<BundleProtocolAgent>()->GetConvergenceLayerAgent();
	Ptr<Link> link = CreateObject<Link> ();

	link->SetLinkLostCallback(MakeCallback(&ConvergenceLayerAgent::LinkLost, cla));

	link->SetRemoteEndpointId(eid);
	link->SetRemoteAddress(address);
	link->SetProbability(1.0);

	return link;
}

/* sergiosvieira */

void RTProphet::DoSendHello(Ptr<Socket> socket, BundleEndpointId eid) {

        if(Simulator::Now ().GetSeconds () > curTime - 10)
	{
		m_nda->Stop();
		while(!m_bundleList.empty()){m_bundleList.pop_front();}
	}
        else
        {
	        ProphetHelloHeader header;
	        header.SetBundleEndpointId(eid);

	        NS_LOG_DEBUG("(" << m_node->GetId() <<")" << " eid: " <<eid);


	        /* monta a mensagem passando a eid dos vizinhos e a probabilidade de entrega deles */
	        NS_LOG_DEBUG("Size of probtable " << m_prob_table.size());
	        for (ProbTable::iterator it = m_prob_table.begin(); it != m_prob_table.end(); ++it) {
	        	for (ProbabilitiesList::iterator itl = (*it).second.prob.begin(); itl != (*it).second.prob.end(); ++itl){
					if (eid.GetId() != (*itl).first.GetId()) {
						//header.addProbability((*it).first, (*it).second.probability);
						//NS_LOG_DEBUG("(" << m_node->GetId() <<")"<< "it.first " << (*it).first.GetId());
						header.addProbability( (*itl).first, (*it).second.prob[(*it).first] );
					}
	        	}
	        }

	        Ptr<Packet> hello = Create<Packet>();
	        /* Defino que o identificador de ProphetHelloHeader vale 1 */
	        TypeTag type(1);
	        hello->AddPacketTag(type);
	        hello->AddHeader(header);
	        socket->Send(hello);
        }
	//m_sendLogger(hello);
}

void RTProphet::DoHandleHello(Ptr<Socket> socket) {
	Ptr<MobilityModel> mm = m_node->GetObject<MobilityModel> ();



	Ptr<Packet> receivedHello;
	Address fromAddress;

	receivedHello = socket->RecvFrom(fromAddress);

	/* Toda vez que criar um novo Header deve-se escolher um valor para ele, e isto deve ser colocado dentro
	 * LinkManager::DiscoveredLink
	 */
	ProphetHelloHeader header;
	receivedHello->PeekHeader(header);

	NS_LOG_DEBUG ("(" << m_node->GetId () << ") - " << mm->GetPosition () << " From " << header.GetBundleEndpointId());

	/* atualizar minha probabilidade */
	updateDeliveryPredFor(header.GetBundleEndpointId());
	updateTransitivePreds(header.GetBundleEndpointId(), header.GetNeighList());
	PrintTable();

	Simulator::ScheduleNow(&NeighbourhoodDetectionAgent::NotifyDiscoveredLink, m_nda, receivedHello, fromAddress);
}

void RTProphet::updateDeliveryPredFor(BundleEndpointId host) {
	ProbTable::iterator tmp;
	tmp = m_prob_table.find(host);

	double oldValue = PINIT;
	double newValue = PINIT;

	if (tmp != m_prob_table.end()) {
		NS_LOG_DEBUG("(" << m_node->GetId() << ") PRO Found " << host);
		oldValue = (*tmp).second.prob[host];
		newValue = oldValue + (1.0 - oldValue) * PINIT;
		(*tmp).second.prob[host] = newValue;
	} else {
		NS_LOG_DEBUG("(" << m_node->GetId() << ") PRO Not Found " << host);
		addNeigh(host, host, newValue);
	}

	NS_LOG_DEBUG("(" << m_node->GetId() << ") PRO Old = " << oldValue << " Probability = " <<  newValue << " " << host);

}

void RTProphet::updateTransitivePreds(BundleEndpointId host,
		ProbabilitiesList list) {
	ProbTable::iterator tmp;
	double pForHost;

	tmp = m_prob_table.find(host);

	if (tmp != m_prob_table.end()) {
		pForHost = (*tmp).second.prob[host]; // P(a,b)
	} else {
		pForHost = 0.0;
	}

	for (ProbabilitiesList::iterator m = list.begin(); m != list.end(); ++m) {

		if ((*m).first == m_eid) {
			continue;
		} NS_LOG_DEBUG("(" << m_node->GetId() << ") my_eid = " << m_eid << " neigh_eid = " << (*m).first << " Diferente");
		double pOld;

		ProbabilitiesList::iterator im;

		im = m_prob_table[host].prob.find((*m).first);

		if (im != m_prob_table[host].prob.end()) {
			pOld = (*im).second;// P(a,c)_old
		} else {
			pOld = 0.0;
		}

		double pNew = pOld + (1.0 - pOld) * pForHost * (*m).second * BETA;
		addNeigh(host, (*m).first, pNew);
		NS_LOG_DEBUG("(" << m_node->GetId() << ") PRO neigh_eid = " << host << " dst_eid = " << (*m).first << " p = " << pNew);

	}
}

/*Joao*/
void RTProphet::ProbabilityReduce() {
	double P_a_b_old, k, pNew;
	for (ProbTable::iterator m = m_prob_table.begin(); m != m_prob_table.end(); ++m) {
		for (ProbabilitiesList::iterator it = (*m).second.prob.begin(); it != (*m).second.prob.end(); ++it){

			P_a_b_old = (*it).second;

			k = Simulator::Now().GetSeconds() - (*m).second.time_k;

			pNew = P_a_b_old * pow(GAMA, k);

			(*it).second = pNew;

			//NS_LOG_DEBUG("(" << m_node->GetId() << ") PRO " <<  (*m).first << " x " << (*m).second.dst_eid << " = " << (*m).second.probability);
			//addNeigh((*m).first, (*m).second.dst_eid, pNew);
		}
		(*m).second.time_k = Simulator::Now().GetSeconds();
	}
}
/*Joao*/

void RTProphet::addNeigh(BundleEndpointId eid, BundleEndpointId dst_eid, double probability) {
	NS_LOG_DEBUG("(" << m_node->GetId() <<")" << eid <<" " << dst_eid);
	/*m_prob_table[eid].dst_eid = dst_eid;
	m_prob_table[eid].probability = probability;*/
	m_prob_table[eid].prob[dst_eid] = probability;
	m_prob_table[eid].time_k = Simulator::Now().GetSeconds();
}
Transitive RTProphet::getNeigh(BundleEndpointId eid) {
	return m_prob_table[eid];
}

bool RTProphet::DoAcceptCustody(Ptr<Bundle> bundle,
				CustodySignalReason& reason)
{
	return true;
}

/* sergiosvieira */


void RTProphet::PrintTable()
{
	NS_LOG_DEBUG("Table of " <<"(" << m_node->GetId() <<")");
	for (ProbTable::iterator m = m_prob_table.begin(); m != m_prob_table.end(); ++m) {
		    NS_LOG_DEBUG("Neigh: ("<<(*m).first <<")");
			for (ProbabilitiesList::iterator it = (*m).second.prob.begin(); it != (*m).second.prob.end(); ++it){
				NS_LOG_DEBUG((*it).first <<" "<<(*it).second);
			}
			NS_LOG_DEBUG("====================");
	}
}


}
} // namespace bundleProtocol, ns3
