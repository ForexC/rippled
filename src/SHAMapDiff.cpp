#include "SHAMap.h"

#include <stack>

// This code is used to compare another node's transaction tree
// to our own. It returns a map containing all items that are different
// between two SHA maps. It is optimized not to descend down tree
// branches with the same branch hash. A limit can be passed so
// that we will abort early if a node sends a map to us that
// makes no sense at all. (And our sync algorithm will avoid
// synchronizing matching brances too.)

class SHAMapDiffNode
{
	public:
	SHAMapNode mNodeID;
	uint256 mOurHash, mOtherHash;
	
	SHAMapDiffNode(SHAMapNode id, const uint256& ourHash, const uint256& otherHash) :
		mNodeID(id), mOurHash(ourHash), mOtherHash(otherHash) { ; }
};

bool SHAMap::walkBranch(SHAMapTreeNode::pointer node, SHAMapItem::pointer otherMapItem, bool isFirstMap,
	SHAMapDiff& differences, int& maxCount)
{
	// Walk a branch of a SHAMap that's matched by an empty branch or single item in the other map
	std::stack<SHAMapTreeNode::pointer> nodeStack;
	nodeStack.push(node);
	
	while(!nodeStack.empty())
	{
		SHAMapTreeNode::pointer node=nodeStack.top();
		nodeStack.pop();
		if(node->isInner())
		{ // This is an inner node, add all non-empty branches
			for(int i=0; i<16; i++)
				if(!node->isEmptyBranch(i))
				{
					SHAMapTreeNode::pointer newNode=getNode(node->getChildNodeID(i), node->getChildHash(i), false);
					if(!newNode) throw SHAMapException(MissingNode);
					nodeStack.push(newNode);
				}
		}
		else
		{ // This is a leaf node, process its item
			SHAMapItem::pointer item=node->getItem();

			if(otherMapItem && otherMapItem->getTag()<item->getTag())
			{ // this item comes after the item from the other map, so add the other item
				if(isFirstMap) // this is first map, so other item is from second
					differences.insert(std::make_pair(otherMapItem->getTag(),
						std::make_pair(SHAMapItem::pointer(), otherMapItem)));
				else
					differences.insert(std::make_pair(otherMapItem->getTag(),
						std::make_pair(otherMapItem, SHAMapItem::pointer())));
				if((--maxCount)<=0) return false;
				otherMapItem=SHAMapItem::pointer();
			}

			if( (!otherMapItem) || (item->getTag()<otherMapItem->getTag()) )
			{ // unmatched
				if(isFirstMap)
					differences.insert(std::make_pair(item->getTag(), std::make_pair(item, SHAMapItem::pointer())));
				else
					differences.insert(std::make_pair(item->getTag(), std::make_pair(SHAMapItem::pointer(), item)));
				if((--maxCount)<=0) return false;
			}
			else if(item->getTag()==otherMapItem->getTag())
			{
				if(item->peekData()!=otherMapItem->peekData())
				{ // non-matching items
					if(isFirstMap)
						differences.insert(std::make_pair(otherMapItem->getTag(),
							std::make_pair(item, otherMapItem)));
					else
						differences.insert(std::make_pair(otherMapItem->getTag(),
							std::make_pair(otherMapItem, item)));
					if((--maxCount)<=0) return false;
					item=SHAMapItem::pointer();
				}
			}
			else assert(false);
		}
	}
	
	if(otherMapItem)
	{ // otherMapItem was unmatched, must add
			if(isFirstMap) // this is first map, so other item is from second
				differences.insert(std::make_pair(otherMapItem->getTag(),
					std::make_pair(SHAMapItem::pointer(), otherMapItem)));
			else
				differences.insert(std::make_pair(otherMapItem->getTag(),
					std::make_pair(otherMapItem, SHAMapItem::pointer())));
			if((--maxCount)<=0) return false;
	}
	
	return true;
}

bool SHAMap::compare(SHAMap::pointer otherMap, SHAMapDiff& differences, int maxCount)
{   // compare two hash trees, add up to maxCount differences to the difference table
	// return value: true=complete table of differences given, false=too many differences
	// throws on corrupt tables or missing nodes

	std::stack<SHAMapDiffNode> nodeStack; // track nodes we've pushed

	ScopedLock sl(Lock());
	if(getHash() == otherMap->getHash()) return true;
	nodeStack.push(SHAMapDiffNode(SHAMapNode(), getHash(), otherMap->getHash()));

 	while(!nodeStack.empty())
 	{
		SHAMapDiffNode dNode(nodeStack.top());
 		nodeStack.pop();

		SHAMapTreeNode::pointer ourNode=getNode(dNode.mNodeID, dNode.mOurHash, false);
		SHAMapTreeNode::pointer otherNode=otherMap->getNode(dNode.mNodeID, dNode.mOtherHash, false);
		if(!ourNode || !otherNode) throw SHAMapException(MissingNode);

		if(ourNode->isLeaf() && otherNode->isLeaf())
		{ // two leaves
			if(ourNode->getTag() == otherNode->getTag())
			{
				if(ourNode->peekData()!=otherNode->peekData())
				{
					differences.insert(std::make_pair(ourNode->getTag(),
						std::make_pair(ourNode->getItem(), otherNode->getItem())));
					if((--maxCount)<=0) return false;
				}
			}
			else
			{
				differences.insert(std::make_pair(ourNode->getTag(),
					std::make_pair(ourNode->getItem(), SHAMapItem::pointer())));
				if((--maxCount)<=0) return false;
				differences.insert(std::make_pair(otherNode->getTag(),
					std::make_pair(SHAMapItem::pointer(), otherNode->getItem())));
				if((--maxCount)<=0) return false;
			}
		}
		else if(ourNode->isInner() && otherNode->isLeaf())
		{
			if(!walkBranch(ourNode, otherNode->getItem(), true, differences, maxCount))
				return false;
		}
		else if(ourNode->isLeaf() && otherNode->isInner())
		{
			if(!otherMap->walkBranch(otherNode, ourNode->getItem(), false, differences, maxCount))
				return false;
		}
		else if(ourNode->isInner() && otherNode->isInner())
		{
			for(int i=0; i<16; i++)
				if(ourNode->getChildHash(i) != otherNode->getChildHash(i) )
				{
					if(!otherNode->getChildHash(i))
					{ // We have a branch, the other tree does not
						SHAMapTreeNode::pointer iNode=getNode(ourNode->getChildNodeID(i),
							ourNode->getChildHash(i), false);
						if(!iNode) throw SHAMapException(MissingNode);
						if(!walkBranch(iNode, SHAMapItem::pointer(), true, differences, maxCount))
							return false;
					}
					else if(!ourNode->getChildHash(i))
					{ // The other tree has a branch, we do not
						SHAMapTreeNode::pointer iNode=otherMap->getNode(otherNode->getChildNodeID(i),
							otherNode->getChildHash(i), false);
						if(!iNode) throw SHAMapException(MissingNode);
						if(!otherMap->walkBranch(iNode, SHAMapItem::pointer(), false, differences, maxCount))
							return false;
					}
					else // The two trees have different non-empty branches
						nodeStack.push(SHAMapDiffNode(ourNode->getChildNodeID(i),
							ourNode->getChildHash(i), otherNode->getChildHash(i)));
				}
		}
		else assert(false);
	}

	return true;
}