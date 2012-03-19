
#include <cstring>

#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/smart_ptr/make_shared.hpp>

#include <openssl/sha.h>

#include "Serializer.h"
#include "BitcoinUtil.h"
#include "SHAMap.h"

std::string SHAMapNode::getString() const
{
	std::string ret="NodeID(";
	ret+=boost::lexical_cast<std::string>(mDepth);
	ret+=",";
	ret+=mNodeID.GetHex();
	ret+=")";
	return ret;
}

uint256 SHAMapNode::smMasks[64];

bool SHAMapNode::operator<(const SHAMapNode &s) const
{
	if(s.mDepth<mDepth) return true;
	if(s.mDepth>mDepth) return false;
	return mNodeID<s.mNodeID;
}

bool SHAMapNode::operator>(const SHAMapNode &s) const
{
	if(s.mDepth<mDepth) return false;
	if(s.mDepth>mDepth) return true;
	return mNodeID>s.mNodeID;
}

bool SHAMapNode::operator<=(const SHAMapNode &s) const
{
	if(s.mDepth<mDepth) return true;
	if(s.mDepth>mDepth) return false;
	return mNodeID<=s.mNodeID;
}

bool SHAMapNode::operator>=(const SHAMapNode &s) const
{
	if(s.mDepth<mDepth) return false;
	if(s.mDepth>mDepth) return true;
	return mNodeID>=s.mNodeID;
}

bool SHAMapNode::operator==(const SHAMapNode &s) const
{
	return (s.mDepth==mDepth) && (s.mNodeID==mNodeID);
}

bool SHAMapNode::operator!=(const SHAMapNode &s) const
{
	return (s.mDepth!=mDepth) || (s.mNodeID!=mNodeID);
}

bool SHAMapNode::operator==(const uint256 &s) const
{
	return s==mNodeID;
}

bool SHAMapNode::operator!=(const uint256 &s) const
{
	return s!=mNodeID;
}

void SHAMapNode::ClassInit()
{ // set up the depth masks
	uint256 selector;
	for(int i=0; i<64; i+=2)
	{
		smMasks[i]=selector;
		*(selector.begin()+(i/2))=0x0F;
		smMasks[i+1]=selector;
		*(selector.begin()+(i/2))=0xFF;
	}
}

uint256 SHAMapNode::getNodeID(int depth, const uint256& hash)
{
	assert(depth>=0 && depth<64);
	return hash & smMasks[depth];
}

SHAMapNode::SHAMapNode(int depth, const uint256 &hash) : mDepth(depth)
{ // canonicalize the hash to a node ID for this depth
	assert(depth>=0 && depth<64);
	mNodeID = getNodeID(depth, hash);
}

SHAMapNode::SHAMapNode(const void *ptr, int len)
{
	if(len<33) mDepth=-1;
	else
	{
		memcpy(&mNodeID, ptr, 32);
		mDepth=*(static_cast<const unsigned char *>(ptr) + 32);
	}
}

void SHAMapNode::addIDRaw(Serializer &s) const
{
	s.add256(mNodeID);
	s.add8(mDepth);
}

std::string SHAMapNode::getRawString() const
{
	Serializer s(33);
	addIDRaw(s);
	return s.getString();
}

SHAMapNode SHAMapNode::getChildNodeID(int m) const
{ // This can be optimized to avoid the << if needed
	assert((m>=0) && (m<16));

	uint256 child(mNodeID);
	child.PeekAt(mDepth/8) |= m << (4*(mDepth%8));
	return SHAMapNode(mDepth+1, child);
}

int SHAMapNode::selectBranch(const uint256& hash) const
{ // Which branch would contain the specified hash
	if(mDepth==63)
	{
		assert(false);
		return -1;
	}
	if((hash&smMasks[mDepth])!=mNodeID)
	{
		std::cerr << "selectBranch(" << getString() << std::endl;
		std::cerr << "  " << hash.GetHex() << " off branch" << std::endl;
		assert(false);
		return -1;	// does not go under this node
	}

	int branch=*(hash.begin()+(mDepth/2));
	if(mDepth%2) branch>>=4;
	else branch&=0xf;

	assert(branch>=0 && branch<16);
	return branch;
}

void SHAMapNode::dump() const
{
	std::cerr << getString() << std::endl;
}

SHAMapTreeNode::SHAMapTreeNode(const SHAMapNode& nodeID, uint32 seq) : SHAMapNode(nodeID), mHash(0), mSeq(seq),
	mType(tnERROR), mFullBelow(false)
{
}

SHAMapTreeNode::SHAMapTreeNode(const SHAMapTreeNode& node, uint32 seq) : SHAMapNode(node),
		mHash(node.mHash), mItem(node.mItem), mSeq(seq), mType(node.mType), mFullBelow(false)
{
	if(node.mItem)
		mItem=boost::make_shared<SHAMapItem>(*node.mItem);
	else
		memcpy(mHashes, node.mHashes, sizeof(mHashes));
}

SHAMapTreeNode::SHAMapTreeNode(const SHAMapNode& node, SHAMapItem::pointer item, TNType type, uint32 seq) :
	SHAMapNode(node), mItem(item), mSeq(seq), mType(type), mFullBelow(true)
{
	assert(item->peekData().size()>=12);
	updateHash();
}

SHAMapTreeNode::SHAMapTreeNode(const SHAMapNode& id, const std::vector<unsigned char>& rawNode, uint32 seq)
	: SHAMapNode(id), mSeq(seq), mType(tnERROR), mFullBelow(false)
{
	Serializer s(rawNode);

	int type=s.removeLastByte();
	int len=s.getLength();
	if( (type<0) || (type>3) || (len<32) ) throw SHAMapException(InvalidNode);

	if(type==0)
	{ // transaction
		mItem=boost::make_shared<SHAMapItem>(s.getSHA512Half(), s.peekData());
		mType=tnTRANSACTION;
	}
	else if(type==1)
	{ // account state
		uint160 u;
		s.get160(u, len-20);
		s.chop(20);
		if(u.isZero()) throw SHAMapException(InvalidNode);
		mItem=boost::make_shared<SHAMapItem>(u, s.peekData());
		mType=tnACCOUNT_STATE;
	}
	else if(type==2)
	{ // full inner
		if(len!=512) throw SHAMapException(InvalidNode);
		for(int i=0; i<16; i++)
			s.get256(mHashes[i], i*32);
		mType=tnINNER;
	}
	else if(type==3)
	{ // compressed inner
		for(int i=0; i<(len/33); i++)
		{
			int pos;
			s.get8(pos, 32+(i*33));
			if( (pos<0) || (pos>=16)) throw SHAMapException(InvalidNode);
			s.get256(mHashes[pos], i*33);
		}
		mType=tnINNER;
	}

	updateHash();
}

void SHAMapTreeNode::addRaw(Serializer &s)
{
	if(mType==tnERROR) throw SHAMapException(InvalidNode);

	if(mType==tnTRANSACTION)
	{
		mItem->addRaw(s);
		s.add8(0);
		assert(s.getLength()>32);
		return;
	}

	if(mType==tnACCOUNT_STATE)
	{
		mItem->addRaw(s);
		s.add160(mItem->getTag().to160());
		s.add8(1);
		return;
	}

	if(getBranchCount()<12)
	{ // compressed node
		for(int i=0; i<16; i++)
			if(mHashes[i].isNonZero())
			{
				s.add256(mHashes[i]);
				s.add8(i);
			}
		s.add8(3);
		return;
	}

	for(int i=0; i<16; i++)
		s.add256(mHashes[i]);
	s.add8(2);
}

bool SHAMapTreeNode::updateHash()
{
	uint256 nh;

	if(mType==tnINNER)
	{
		bool empty=true;
		for(int i=0; i<16; i++)
			if(mHashes[i].isNonZero())
			{
				empty=false;
				break;
			}
		if(!empty)
			nh=Serializer::getSHA512Half(reinterpret_cast<unsigned char *>(mHashes), sizeof(mHashes));
	}
	else if(mType==tnACCOUNT_STATE)
	{
		Serializer s;
		mItem->addRaw(s);
		s.add160(mItem->getTag().to160());
		nh=s.getSHA512Half();
	}
	else if(mType==tnTRANSACTION)
	{
		nh=Serializer::getSHA512Half(mItem->peekData());
	}
	else assert(false);

	if(nh==mHash) return false;
	mHash=nh;
	return true;
}

bool SHAMapTreeNode::setItem(SHAMapItem::pointer& i, TNType type)
{
	uint256 hash=getNodeHash();
	mType=type;
	mItem=i;
	assert(isLeaf());
	updateHash();
	return getNodeHash()==hash;
}

SHAMapItem::pointer SHAMapTreeNode::getItem() const
{
	assert(isLeaf());
	return boost::make_shared<SHAMapItem>(*mItem);
}

int SHAMapTreeNode::getBranchCount() const
{
	assert(isInner());
	int ret=0;
	for(int i=0; i<16; ++i)
		if(mHashes[i].isNonZero()) ++ret;
	return ret;
}

void SHAMapTreeNode::makeInner()
{
	mItem=SHAMapItem::pointer();
	memset(mHashes, 0, sizeof(mHashes));
	mType=tnINNER;
	mHash.zero();
}

void SHAMapTreeNode::dump()
{
	std::cerr << "SHAMapTreeNode(" << getNodeID().GetHex() << ")" << std::endl;
}

std::string SHAMapTreeNode::getString() const
{
	std::string ret="NodeID(";
	ret+=boost::lexical_cast<std::string>(getDepth());
	ret+=",";
	ret+=getNodeID().GetHex();
	ret+=")";
	if(isInner())
	{
		for(int i=0; i<16; i++)
			if(!isEmptyBranch(i))
			{
				ret+=",b";
				ret+=boost::lexical_cast<std::string>(i);
			}
	}
	if(isLeaf())
	{
		ret+=",leaf";
	}
	return ret;
}

bool SHAMapTreeNode::setChildHash(int m, const uint256 &hash)
{
	assert( (m>=0) && (m<16) );
	assert(mType==tnINNER);
	if(mHashes[m]==hash)
		return false;
	mHashes[m]=hash;
	return updateHash();
}

const uint256& SHAMapTreeNode::getChildHash(int m) const
{
	assert( (m>=0) && (m<16) && (mType==tnINNER) );
	return mHashes[m];
}