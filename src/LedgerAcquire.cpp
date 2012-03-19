
#include "boost/foreach.hpp"
#include "boost/make_shared.hpp"

#include "Application.h"
#include "LedgerAcquire.h"

LedgerAcquire::LedgerAcquire(const uint256& hash) : mHash(hash),
	mComplete(false), mFailed(false), mHaveBase(false), mHaveState(false), mHaveTransactions(false)
{
	;
}

void LedgerAcquire::done()
{
	std::vector< boost::function<void (LedgerAcquire::pointer)> > triggers;

	mLock.lock();
	triggers=mOnComplete;
	mOnComplete.empty();
	mLock.unlock();

	for(int i=0; i<triggers.size(); i++)
		triggers[i](shared_from_this());
}

void LedgerAcquire::setTimer()
{
	// WRITEME
}

void LedgerAcquire::timerEntry(boost::weak_ptr<LedgerAcquire> wptr)
{
	LedgerAcquire::pointer ptr=wptr.lock();
	if(ptr) ptr->trigger(true);
}

void LedgerAcquire::addOnComplete(boost::function<void (LedgerAcquire::pointer)> trigger)
{
	mLock.lock();
	mOnComplete.push_back(trigger);
	mLock.unlock();
}

void LedgerAcquire::trigger(bool timer)
{
	if(mComplete || mFailed) return;

	if(!mHaveBase)
	{
		// WRITEME: Do we need to search for peers?
		boost::shared_ptr<newcoin::TMGetLedger> tmGL=boost::make_shared<newcoin::TMGetLedger>();
		tmGL->set_ledgerhash(mHash.begin(), mHash.size());
		tmGL->set_itype(newcoin::liBASE);
		sendRequest(tmGL);
	}

	if(mHaveBase && !mHaveTransactions)
	{
		assert(mLedger);
		if(mLedger->peekTransactionMap()->getHash().isZero())
		{ // we need the root node
			boost::shared_ptr<newcoin::TMGetLedger> tmGL=boost::make_shared<newcoin::TMGetLedger>();
			tmGL->set_ledgerhash(mHash.begin(), mHash.size());
			tmGL->set_ledgerseq(mLedger->getLedgerSeq());
			tmGL->set_itype(newcoin::liTX_NODE);
			*(tmGL->add_nodeids())=SHAMapNode().getRawString();
			sendRequest(tmGL);
		}
		else
		{
			std::vector<SHAMapNode> nodeIDs;
			std::vector<uint256> nodeHashes;
			mLedger->peekTransactionMap()->getMissingNodes(nodeIDs, nodeHashes, 128);
			if(nodeIDs.empty())
			{
				if(!mLedger->peekTransactionMap()->isValid()) mFailed=true;
				else
				{
					mHaveTransactions=true;
					if(mHaveState) mComplete=true;
				}
			}
			else
			{
				boost::shared_ptr<newcoin::TMGetLedger> tmGL=boost::make_shared<newcoin::TMGetLedger>();
				tmGL->set_ledgerhash(mHash.begin(), mHash.size());
				tmGL->set_ledgerseq(mLedger->getLedgerSeq());
				tmGL->set_itype(newcoin::liTX_NODE);
				for(std::vector<SHAMapNode>::iterator it=nodeIDs.begin(); it!=nodeIDs.end(); ++it)
					*(tmGL->add_nodeids())=it->getRawString();
				sendRequest(tmGL);
			}
		}
	}

	if(mHaveBase && !mHaveState)
	{
		assert(mLedger);
		if(mLedger->peekAccountStateMap()->getHash().isZero())
		{ // we need the root node
			boost::shared_ptr<newcoin::TMGetLedger> tmGL=boost::make_shared<newcoin::TMGetLedger>();
			tmGL->set_ledgerhash(mHash.begin(), mHash.size());
			tmGL->set_ledgerseq(mLedger->getLedgerSeq());
			tmGL->set_itype(newcoin::liAS_NODE);
			*(tmGL->add_nodeids())=SHAMapNode().getRawString();
			sendRequest(tmGL);
		}
		else
		{
			std::vector<SHAMapNode> nodeIDs;
			std::vector<uint256> nodeHashes;
			mLedger->peekAccountStateMap()->getMissingNodes(nodeIDs, nodeHashes, 128);
			if(nodeIDs.empty())
			{
 				if(!mLedger->peekAccountStateMap()->isValid()) mFailed=true;
				else
				{
					mHaveState=true;
					if(mHaveTransactions) mComplete=true;
				}
			}
			else
			{
				boost::shared_ptr<newcoin::TMGetLedger> tmGL=boost::make_shared<newcoin::TMGetLedger>();
				tmGL->set_ledgerhash(mHash.begin(), mHash.size());
				tmGL->set_ledgerseq(mLedger->getLedgerSeq());
				tmGL->set_itype(newcoin::liAS_NODE);
				for(std::vector<SHAMapNode>::iterator it=nodeIDs.begin(); it!=nodeIDs.end(); ++it)
					*(tmGL->add_nodeids())=it->getRawString();
				sendRequest(tmGL);
			}
		}
	}

	if(mComplete || mFailed)
		done();
	else if(timer)
		setTimer();
}

void LedgerAcquire::sendRequest(boost::shared_ptr<newcoin::TMGetLedger> tmGL)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	if(mPeers.empty()) return;

	PackedMessage::pointer packet=boost::make_shared<PackedMessage>(tmGL, newcoin::mtGET_LEDGER);

	std::list<boost::weak_ptr<Peer> >::iterator it=mPeers.begin();
	while(it!=mPeers.end())
	{
		if(it->expired())
			mPeers.erase(it++);
		else
		{
			// FIXME: Track last peer sent to and time sent
			it->lock()->sendPacket(packet);
			return;
		}
	}
}

void LedgerAcquire::peerHas(Peer::pointer ptr)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	std::list<boost::weak_ptr<Peer> >::iterator it=mPeers.begin();
	while(it!=mPeers.end())
	{
		Peer::pointer pr=it->lock();
		if(!pr) // we have a dead entry, remove it
			it=mPeers.erase(it);
		else
		{
			if(pr->samePeer(ptr)) return;	// we already have this peer
			++it;
		}
	}
	mPeers.push_back(ptr);
}

void LedgerAcquire::badPeer(Peer::pointer ptr)
{
	boost::recursive_mutex::scoped_lock sl(mLock);
	std::list<boost::weak_ptr<Peer> >::iterator it=mPeers.begin();
	while(it!=mPeers.end())
	{
		Peer::pointer pr=it->lock();
		if(!pr) // we have a dead entry, remove it
			it=mPeers.erase(it);
		else
		{
			if(ptr->samePeer(pr))
			{ // We found a pointer to the bad peer
				mPeers.erase(it);
				return;
			}
			++it;
		}
	}
}

bool LedgerAcquire::takeBase(const std::string& data)
{ // Return value: true=normal, false=bad data
	boost::recursive_mutex::scoped_lock sl(mLock);
	if(mHaveBase) return true;
	Ledger* ledger=new Ledger(data);
	if(ledger->getHash()!=mHash)
	{
		delete ledger;
		return false;
	}
	mLedger=Ledger::pointer(ledger);
	mLedger->setAcquiring();
	mHaveBase=true;
	return true;
}

bool LedgerAcquire::takeTxNode(const std::list<SHAMapNode>& nodeIDs,
	const std::list<std::vector<unsigned char> >& data)
{
	if(!mHaveBase) return false;
	std::list<SHAMapNode>::const_iterator nodeIDit=nodeIDs.begin();
	std::list<std::vector<unsigned char> >::const_iterator nodeDatait=data.begin();
	while(nodeIDit!=nodeIDs.end())
	{
		if(!mLedger->peekTransactionMap()->addKnownNode(*nodeIDit, *nodeDatait))
			return false;
		++nodeIDit;
		++nodeDatait;
	}
	if(!mLedger->peekTransactionMap()->isSynching()) mHaveTransactions=true;
	return true;
}

bool LedgerAcquire::takeAsNode(const std::list<SHAMapNode>& nodeIDs,
	const std::list<std::vector<unsigned char> >& data)
{
	if(!mHaveBase) return false;
	std::list<SHAMapNode>::const_iterator nodeIDit=nodeIDs.begin();
	std::list<std::vector<unsigned char> >::const_iterator nodeDatait=data.begin();
	while(nodeIDit!=nodeIDs.end())
	{
		if(!mLedger->peekAccountStateMap()->addKnownNode(*nodeIDit, *nodeDatait))
			return false;
		++nodeIDit;
		++nodeDatait;
	}
	if(!mLedger->peekAccountStateMap()->isSynching()) mHaveState=true;
	return true;
}

LedgerAcquire::pointer LedgerAcquireMaster::findCreate(const uint256& hash)
{
	boost::mutex::scoped_lock sl(mLock);
	LedgerAcquire::pointer& ptr=mLedgers[hash];
	if(ptr) return ptr;
	return boost::make_shared<LedgerAcquire>(hash);
}

LedgerAcquire::pointer LedgerAcquireMaster::find(const uint256& hash)
{
	boost::mutex::scoped_lock sl(mLock);
	std::map<uint256, LedgerAcquire::pointer>::iterator it=mLedgers.find(hash);
	if(it!=mLedgers.end()) return it->second;
	return LedgerAcquire::pointer();
}

bool LedgerAcquireMaster::hasLedger(const uint256& hash)
{
	boost::mutex::scoped_lock sl(mLock);
	return mLedgers.find(hash)!=mLedgers.end();
}

bool LedgerAcquireMaster::gotLedgerData(newcoin::TMLedgerData& packet)
{
	uint256 hash;
	if(packet.ledgerhash().size()!=32) return false;
	memcpy(&hash, packet.ledgerhash().data(), 32);

	LedgerAcquire::pointer ledger=find(hash);
	if(!ledger) return false;

	if(packet.type()==newcoin::liBASE)
	{
		if(packet.nodes_size()!=1) return false;
		const newcoin::TMLedgerNode& node=packet.nodes(0);
		if(!node.has_nodedata()) return false;
		return ledger->takeBase(node.nodedata());
	}
	else if( (packet.type()==newcoin::liTX_NODE) || (packet.type()==newcoin::liAS_NODE) )
	{
		std::list<SHAMapNode> nodeIDs;
		std::list<std::vector<unsigned char> > nodeData;

		if(packet.nodes().size()<=0) return false;
		for(int i=0; i<packet.nodes().size(); i++)
		{
			const newcoin::TMLedgerNode& node=packet.nodes(i);
			if(!node.has_nodeid() || !node.has_nodedata()) return false;

			nodeIDs.push_back(SHAMapNode(node.nodeid().data(), node.nodeid().size()));
			nodeData.push_back(std::vector<unsigned char>(node.nodedata().begin(), node.nodedata().end()));
		}
		if(packet.type()==newcoin::liTX_NODE) return ledger->takeTxNode(nodeIDs, nodeData);
		else return ledger->takeAsNode(nodeIDs, nodeData);
	}
	else return false;
}