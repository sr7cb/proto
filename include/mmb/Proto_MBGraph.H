#pragma once

#ifndef _PROTO_MB_GRAPH_
#define _PROTO_MB_GRAPH_

#include "Proto.H"
#include "Proto_CoordPermutation.H"

namespace Proto
{
    unsigned int codim(Point a_dir);
   
    std::vector<Point> codimDirs(unsigned int a_codim);

    struct MBGraphNode;
    typedef std::pair<std::shared_ptr<MBGraphNode>, Point> ArcPair;
    
    struct MBGraphNode
    {
        MBGraphNode(unsigned int a_index);
     
        void addArc(Point a_dir, ArcPair& a_arc);

        unsigned int index;
        std::map<Point, std::vector<ArcPair>> boundaries;
        std::map<Point, CoordPermutation> rotations;
    };
  
    class MBProblemDomain;

    class MBGraph
    {
        friend class MBProblemDomain;
        public:

        inline MBGraph(unsigned int a_numBlocks);

        inline void defineBoundary(
                unsigned int        a_srcBlock,
                unsigned int        a_dstBlock,
                unsigned int        a_dir,
                Side::LoHiSide      a_side,
                CoordPermutation&   a_rotation);

        inline void defineBoundary(
                unsigned int        a_srcBlock,
                unsigned int        a_dstBlock,
                Point               a_dir,
                CoordPermutation&   a_rotation);

        inline unsigned int size() const {return m_blocks.size(); }

        inline unsigned int numBoundaries(unsigned int a_blockIndex, Point& a_dir) const;

        inline std::set<Point> connectivity(unsigned int a_srcBlock, unsigned int a_dstBlock) const;

        inline unsigned int adjacent(
                unsigned int    a_srcBlock,
                unsigned int    a_dir,
                Side::LoHiSide  a_side) const;

        inline unsigned int adjacent(
                unsigned int    a_srcBlock,
                Point           a_dir) const;

        inline void print() const;
    
        private:

        std::vector<std::shared_ptr<MBGraphNode>> m_blocks;
    };
#include "implem/Proto_MBGraphImplem.H"
} // end namespace Proto
#endif // end include guard