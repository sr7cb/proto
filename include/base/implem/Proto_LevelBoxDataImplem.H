template<typename T, unsigned int C, MemType MEM, Centering CTR>
LevelBoxData<T, C, MEM, CTR>::
LevelBoxData()
{
    m_isDefined = false;
}

template<typename T, unsigned int C, MemType MEM, Centering CTR>
LevelBoxData<T, C, MEM, CTR>::
LevelBoxData(const DisjointBoxLayout& a_layout, 
             const Point& a_ghost)
{
    define(a_layout, a_ghost);
}
template<typename T, unsigned int C, MemType MEM, Centering CTR>
void LevelBoxData<T, C, MEM, CTR>::define(
                 const DisjointBoxLayout& a_layout, 
                 const Point& a_ghost)
{
    m_isDefined = true;
    m_ghost = a_ghost;
    m_layout = a_layout;
    for (auto dit = begin(); dit.ok(); ++dit)
    {
        //We don't allow LevelBoxData to use StackAllocator in allocating BoxDatas
        auto patch = std::make_shared<BoxData<T,C,MEM>>(dit.box().grow(m_ghost));
        m_data.push_back(patch);
    }

    if (m_ghost != Point::Zeros())
    {
        m_exchangeCopier.define(LevelCopierOp<T, C, MEM, CTR>(*this, *this));
    }
}

template<typename T, unsigned int C, MemType MEM, Centering CTR>
template<typename Func, typename... Srcs>
void LevelBoxData<T, C, MEM, CTR>::initialize(Func& a_func, Srcs... a_srcs)
{
    for (auto iter = this->begin(); iter.ok(); ++iter)
    {
        auto& patch = this->operator[](*iter);
        forallInPlace_p(a_func, patch, a_srcs...);
    }
}

template<typename T, unsigned int C, MemType MEM, Centering CTR>
template<typename Func, typename... Srcs>
void LevelBoxData<T, C, MEM, CTR>::initConvolve(Func& a_func, Srcs... a_srcs)
{
    for (auto iter = this->begin(); iter.ok(); ++iter)
    {
        auto& patch = this->operator[](*iter);
        BoxData<T, C, MEM> temp(patch.box().grow(1));
        forallInPlace_p(a_func, temp, a_srcs...);
        Operator::convolve(patch, temp);
    }
}

template<typename T, unsigned int C, MemType MEM, Centering CTR>
unsigned int
LevelBoxData<T, C, MEM, CTR>::patchSize() const
{
    int size = 1;
    Point boxSize = layout().boxSize() + 2*ghost();
    for (int ii = 0; ii < DIM; ii++)
    {
        size *= boxSize[ii];
    }
    return C*size;
}

template<typename T, unsigned int C, MemType MEM, Centering CTR>
unsigned int
LevelBoxData<T, C, MEM, CTR>::offset(int a_proc) const
{
    Point boxSizeVect = layout().boxSize() + 2*ghost();
    int boxSize = 1;
    for (int ii = 0; ii < DIM; ii++)
    {
        boxSize *= boxSizeVect[ii];
    }
    return layout().offset(a_proc)*boxSize*C;
}

template<typename T, unsigned int C, MemType MEM, Centering CTR>
unsigned int 
LevelBoxData<T, C, MEM, CTR>::size() const
{
    return m_layout.size();
}

template<typename T, unsigned int C, MemType MEM, Centering CTR>
void   
LevelBoxData<T, C, MEM, CTR>::setToZero()
{
    for (auto iter = begin(); iter.ok(); ++iter)
    {
        auto& patch = this->operator[](*iter);
        patch.setVal(0);
    }
}

template<typename T, unsigned int C, MemType MEM, Centering CTR>
void 
LevelBoxData<T, C, MEM, CTR>::exchange()
{
    PR_TIME("LevelBoxData::exchange");
#ifdef PR_MPI
    {
        PR_TIME("MPI_Barrier exchange");
        barrier();
    }
#endif
    m_exchangeCopier.execute();
}

template<typename T, unsigned int C, MemType MEM, Centering CTR>
void 
LevelBoxData<T, C, MEM, CTR>::
copyTo(LevelBoxData<T, C, MEM, CTR>& a_dest) const
{
    // we trust that copier will keep *this constant by only moving data to dst
  if (a_dest.layout() == this->layout())
    {
      for (auto dit = a_dest.begin(); dit.ok(); ++dit)
        {
          (*this)[*dit].copyTo(a_dest[*dit]);
        }
      a_dest.exchange();
      return;
    } 
    auto mutableSrc = const_cast<LevelBoxData<T, C, MEM, CTR>*> (this);
    LevelCopierOp<T, C, MEM, CTR> op(*mutableSrc, a_dest);
    LevelCopier<T, C, MEM, CTR> copier;
    copier.define(op);
    //copier.print();
    copier.execute();
}

template<typename T, unsigned int C, MemType MEM, Centering CTR>
void 
LevelBoxData<T, C, MEM, CTR>::
coarsenTo(LevelBoxData<T, C, MEM, CTR>& a_dest, Point a_refRatio) const
{
    if (CTR == PR_CELL)
    {
        auto AVG = Stencil<T>::AvgDown(a_refRatio);
        DisjointBoxLayout cfLayout = m_layout.coarsen(a_refRatio);
        LevelBoxData<T, C, MEM, CTR> cfLevel(cfLayout, Point::Zeros());
        for (auto iter = m_layout.begin(); iter.ok(); ++iter)
        {
            auto& fine = operator[](*iter); 
            auto& crse = cfLevel[*iter];
            
            crse |= AVG(fine);
        }
        cfLevel.copyTo(a_dest);
    } else {
        MayDay<void>::Warning("LevelBoxData::coarsenTo | Warning: Non-Cell centered implementation not yet implemented");
    }
}

template<typename T, unsigned int C, MemType MEM, Centering CTR>
void 
LevelBoxData<T, C, MEM, CTR>::linearIn(
    void* a_buffer,
    const Box& a_box,
    const DataIndex& a_index)
{
    auto& patch = this->operator[](a_index);
    CInterval comps(0, C-1);
    patch.linearIn(a_buffer, a_box, comps);
}

template<typename T, unsigned int C, MemType MEM, Centering CTR>
void 
LevelBoxData<T, C, MEM, CTR>::linearIn(
    void* a_buffer,
    const DataIndex& a_index)
{
    Box b = this->operator[](a_index).box();
    linearIn(a_buffer, b, a_index);
}

template<typename T, unsigned int C, MemType MEM, Centering CTR>
void 
LevelBoxData<T, C, MEM, CTR>::linearIn( void* a_buffer)
{
    char* readPoint = (char*)a_buffer;
    for (auto iter = this->begin(); *iter != iter.end(); ++iter)
    {
        linearIn(readPoint, *iter);
        readPoint += linearSize(*iter); 
    }
}

template<typename T, unsigned int C, MemType MEM, Centering CTR>
void 
LevelBoxData<T, C, MEM, CTR>::linearOut(
    void* a_buffer,
    const Box& a_box,
    const DataIndex& a_index) const
{
    auto& patch = this->operator[](a_index);
    CInterval comps(0, C-1);
    patch.linearOut(a_buffer, a_box, comps);
}

template<typename T, unsigned int C, MemType MEM, Centering CTR>
void 
LevelBoxData<T, C, MEM, CTR>::linearOut(
    void* a_buffer,
    const DataIndex& a_index) const
{
    Box b = this->operator[](a_index).box();
    linearOut(a_buffer, b, a_index);
}

template<typename T, unsigned int C, MemType MEM, Centering CTR>
void 
LevelBoxData<T, C, MEM, CTR>::linearOut( void* a_buffer) const
{
    char* writePoint = (char*)a_buffer;
    for (auto iter = this->begin(); *iter != iter.end(); ++iter)
    {
        linearOut(writePoint, *iter);
        writePoint += linearSize(*iter); 
    }
}

template<typename T, unsigned int C, MemType MEM, Centering CTR>
size_t
LevelBoxData<T, C, MEM, CTR>::linearSize(
    const Box& a_box,
    const DataIndex& a_index) const
{
    return C*a_box.size()*sizeof(T);
}

template<typename T, unsigned int C, MemType MEM, Centering CTR>
size_t
LevelBoxData<T, C, MEM, CTR>::linearSize( const DataIndex& a_index) const
{
    Box b = this->operator[](a_index).box();
    return this->linearSize(b, a_index);
}

template<typename T, unsigned int C, MemType MEM, Centering CTR>
size_t
LevelBoxData<T, C, MEM, CTR>::linearSize() const
{
    size_t totalSize = 0;
    for (auto iter = this->begin(); *iter != iter.end(); ++iter)
    {
        totalSize += size(*iter);
    }
    return totalSize;
}

//// Interaction with iterators.
template<typename T, unsigned int C, MemType MEM, Centering CTR>
DataIterator LevelBoxData<T, C, MEM, CTR>::begin() const
{
    DataIterator retval(m_layout);
    retval.begin();
    return retval;
} 

template<typename T, unsigned int C, MemType MEM, Centering CTR>
BoxData<T,C,MEM>& 
LevelBoxData<T, C, MEM, CTR>::operator[](const DataIndex& a_di) 
{
    //return (*(m_data[m_layout.myIntIndex(a_di)]));
    return (*(m_data[a_di]));
}

template<typename T, unsigned int C, MemType MEM, Centering CTR>
const BoxData<T,C,MEM>& 
LevelBoxData<T, C, MEM, CTR>::operator[](const DataIndex& a_di) const
{
    //return (*(m_data[m_layout.myIntIndex(a_di)]));
    return (*(m_data[a_di]));
}


template<typename T, unsigned int C, MemType MEM, Centering CTR>
void
LevelBoxData<T, C, MEM, CTR>::increment(LevelBoxData<T, C, MEM, CTR>& a_data, T a_scale)
{
    //TODO: Check that layouts are compatable
    PR_TIME("LevelBoxData::increment");
    Stencil<T> INCR = a_scale*Shift::Zeros();
    for (auto iter = m_layout.begin(); iter.ok(); ++iter)
    {
        auto& lhs_i = this->operator[](*iter);
        auto& rhs_i = a_data[*iter];
        lhs_i += INCR(rhs_i); // using a Stencil here fuses the add and multiply in a single kernel launch -clg
    }
}

template<typename T, unsigned int C, MemType MEM, Centering CTR>
template <Proto::Operation OP>
double
LevelBoxData<T, C, MEM, CTR>::reduce(unsigned int a_comp) const
{
    PR_TIME("LevelBoxData::reduce");
    Reduction<T, OP> rxn;
    for (auto iter = m_layout.begin(); iter.ok(); ++iter)
    {
        auto& data = operator[](*iter);
        if (ghost() == Point::Zeros())
        {
            // avoid the copyTo call if there are no ghost cells
            data.reduce(rxn, a_comp);
        } else {
            BoxData<T, 1, MEM> temp(iter.box());
            auto comp = slice(data, a_comp);
            comp.copyTo(temp);
            temp.reduce(rxn);
        }
    }
    double local = rxn.fetch();
#ifdef PR_MPI
    double global = 0;
    switch (OP)
    {
        case Abs:
        case Max:
            MPI_Allreduce(&local, &global, 1, MPI_DOUBLE, MPI_MAX, MPI_COMM_WORLD);
            break;
        case Min:
            MPI_Allreduce(&local, &global, 1, MPI_DOUBLE, MPI_MIN, MPI_COMM_WORLD);
            break;
        case Sum:
        case SumAbs:
            MPI_Allreduce(&local, &global, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
            break;
        default:
            MayDay<void>::Abort("LevelBoxData::reduce | Error: Unknown reduction operator.");
            break;
    }
    return global;
#else
    return local;
#endif
}


template<typename T, unsigned int C, MemType MEM, Centering CTR>
double
LevelBoxData<T, C, MEM, CTR>::absMax(unsigned int a_comp) const
{
    return reduce<Abs>(a_comp);
}

template<typename T, unsigned int C, MemType MEM, Centering CTR>
double
LevelBoxData<T, C, MEM, CTR>::sum(unsigned int a_comp) const
{
    return reduce<Sum>(a_comp);
}

template<typename T, unsigned int C, MemType MEM, Centering CTR>
double
LevelBoxData<T, C, MEM, CTR>::integrate(double a_dx, unsigned int a_comp) const
{
    return reduce<Sum>(a_comp) * pow(a_dx, DIM);
}

template<typename T, unsigned int C, MemType MEM, Centering CTR>
double
LevelBoxData<T, C, MEM, CTR>::integrate(std::array<double, DIM> a_dx, unsigned int a_comp) const
{
    double factor = 1.0;
    for (int dir = 0; dir < DIM; dir++) { factor *= a_dx[dir]; }
    return reduce<Sum>(a_comp) * factor;
}

template<typename T, unsigned int C, MemType MEM, Centering CTR>
double
LevelBoxData<T, C, MEM, CTR>::integrateAbs(double a_dx, unsigned int a_comp) const
{
    return reduce<SumAbs>(a_comp) * pow(a_dx, DIM);
}

template<typename T, unsigned int C, MemType MEM, Centering CTR>
double
LevelBoxData<T, C, MEM, CTR>::integrateAbs(std::array<double, DIM> a_dx, unsigned int a_comp) const
{
    double factor = 1.0;
    for (int dir = 0; dir < DIM; dir++) { factor *= a_dx[dir]; }
    return reduce<SumAbs>(a_comp) * factor;
}

template<typename T, unsigned int C, MemType MEM, Centering CTR>
double
LevelBoxData<T, C, MEM, CTR>::max(unsigned int a_comp) const
{
    return reduce<Max>(a_comp);
}

template<typename T, unsigned int C, MemType MEM, Centering CTR>
double
LevelBoxData<T, C, MEM, CTR>::min(unsigned int a_comp) const
{
    return reduce<Min>(a_comp);
}


// =======================================================================
// LEVEL COPIER OP

template<typename T, unsigned int C, MemType MEM, Centering CTR>
LevelCopierOp<T, C, MEM, CTR>::LevelCopierOp(
        LevelBoxData<T, C, MEM, CTR>& a_src,
        LevelBoxData<T, C, MEM, CTR>& a_dst)
{
    m_src = &a_src;
    m_dst = &a_dst;
}

template<typename T, unsigned int C, MemType MEM, Centering CTR>
int
LevelCopierOp<T, C, MEM, CTR>::linearSize(const Box& a_box,
        const DataIndex& a_index) const
{
    //TODO: Does this do the right thing?
    return C*a_box.size()*sizeof(T);
}

template<typename T, unsigned int C, MemType MEM, Centering CTR>
void
LevelCopierOp<T, C, MEM, CTR>::linearOut(void* a_buf, 
        const Box& a_bx,
        const DataIndex& a_index) const
{
    auto& patch = m_src->operator[](a_index);
    CInterval cint(0,C-1);
    patch.linearOut(a_buf, a_bx, cint);
}

template<typename T, unsigned int C, MemType MEM, Centering CTR>
void
LevelCopierOp<T, C, MEM, CTR>::linearIn(void* a_buf, 
        const Box& a_bx,
        const DataIndex& a_index) const
{
    auto& patch = m_dst->operator[](a_index);
    CInterval cint(0,C-1);
    patch.linearIn(a_buf, a_bx, cint);
}

template<typename T, unsigned int C, MemType MEM, Centering CTR>
void
LevelCopierOp<T, C, MEM, CTR>::localCopy(
        const Box& a_domain, const DataIndex& a_domainIndex,
        const Box& a_range,  const DataIndex& a_rangeIndex) const
{
    int LinearizationTest = 1;
    if (LinearizationTest == 0)
    {
        PR_TIMERS("LOP_localCopy_1");
        auto& src = m_src->operator[](a_domainIndex);
        auto& dst = m_dst->operator[](a_rangeIndex);
        Point shift = a_range.low() - a_domain.low();
        src.copyTo(dst, a_domain, shift);
    }
    else
    {
        PR_TIMERS("LOP_localCopy_2");
        int sizeSrc = linearSize(a_domain, a_domainIndex);
        int sizeDst = linearSize(a_range, a_rangeIndex);

        if (sizeSrc != sizeDst)
        {
            MayDay<void>::Abort("LinearizationTest failure: dest and source have different sizes");
        }

        vector<char> buffer(sizeSrc);
        {
            PR_TIMERS("linearIn / linearOut inside LDOP::op");
            void* b = (void*)&(buffer[0]);
            linearOut(b, a_domain, a_domainIndex);
            linearIn (b, a_range,  a_rangeIndex);
        }
    }
}

// =======================================================================
// LEVEL COPIERS
template<typename T, unsigned int C, MemType MEM, Centering CTR>
    void
LevelCopier<T, C, MEM, CTR>::buildMotionPlans(LevelCopierOp<T, C, MEM, CTR>& a_op)
{
    // Construct Motion items for copyTo and insert into MotionPlans.
    // For each motion item, the source Box is specified relative to the location
    // of the source patch, and the dest Box relative to the location of the dest
    // patch. This is easy for the fromMotionPlan / localMotionPlan. In constructing
    // the toMotionPlan, though, must explicitly shift back and forth between local
    // coordinate systems defined by the source and destination.
    int destProcID = procID();

    DisjointBoxLayout srclayout = a_op.m_src->layout();
    DisjointBoxLayout destlayout = a_op.m_dst->layout();
    ProblemDomain pd = srclayout.domain();
    Point destGhost = a_op.m_dst->ghost();
    PR_assert(pd==destlayout.domain());
    // source pd and dest pd must be the same
    // For each Box on this processor, compute all the pairs of motionItems that move data
    // to this Box.
    for (auto diter = destlayout.begin(); diter.ok(); ++diter) 
    {
        Box bx = diter.box().grow(destGhost);

        // Compute all the patches (as Points in a bitmap) that might move data to bx.

        Box dProbC = bx.coarsen(srclayout.boxSize());
        auto bxit = BoxIterator(dProbC);
        // Iterate over the possible source patches to contruct MotionItems.

        for (bxit.begin();bxit.ok();++bxit)
        {
            // Find the possible source patch, or its periodic image.
            auto di = srclayout.find(*bxit);
            if (di != srclayout.end())
            {
                Box destBx = Box(*bxit,*bxit).refine(srclayout.boxSize()) & bx;
                // Compute srcBx to be the shifted image of destBx. If destBx is in
                // the ProblemDomain box, srcBx == destBx.
                //Box srcBx = Box(pd.image(destBx.low()),pd.image(destBx.high()));

                if (!destBx.empty())
                {
                    // Create MotionItem.
                    // Compute srcBx to be the shifted image of destBx. If destBx 
                    // is in the ProblemDomain box, srcBx == destBx.
                    Box srcBx = Box(pd.image(destBx.low()),
                                    pd.image(destBx.high()));
                    //MotionItem* toMotionItem = new MotionItem(di, *diter, srcBx, destBx);
                    MotionItem toMotionItem(di, *diter, srcBx, destBx);
                    int srcProcID = srclayout.procID(di);
                    if (srcProcID == procID())
                    {
                        PROTO_ASSERT(srcProcID == destlayout.procID(*diter),
                            "LevelCopier::buildMotionPlan | Error: Data corruption.");
                        this->m_localMotionPlan.push_back(toMotionItem);
                    }
                    else
                    {
                        PROTO_ASSERT(srcProcID != destlayout.procID(*diter),
                            "LevelCopier::buildMotionPlan | Error: Data corruption.");
                        toMotionItem.procID = srcProcID;
                        this->m_toMotionPlan.push_back(toMotionItem);
                    }
                }
            }
        }
    }

    // Iterate over Boxes in the source to construct fromMotionPlan  
    for (auto siter = srclayout.begin(); siter.ok(); ++siter)
    {
        // We grow the source Box by dstGhost to capture all the patches that the
        // source will copy to. dProbC contains all the points in the bitmap for
        // the destination. 
        Box dProbC = siter.box().grow(destGhost).coarsen(destlayout.boxSize());
        auto bxit = BoxIterator(dProbC);
        for (bxit.begin();bxit.ok();++bxit)
        {
            auto di = destlayout.find(*bxit);
            if (di != destlayout.end())
            {
                Box bxDestShifted = Box(*bxit,*bxit).refine(destlayout.boxSize()).grow(destGhost);
                Box bxDest = destlayout[di].grow(destGhost);
                //Point shiftSrcDest = bxDestShifted.low() - bxDest.low();
                Point shiftSrcDest = bxDest.low() - bxDestShifted.low();
                bxDest = bxDest & (siter.box().shift(shiftSrcDest));
                Box bxSrc = bxDestShifted&siter.box();
                if (!bxSrc.empty())
                {
                    int destProcID = destlayout.procID(di);           
                    if (destProcID != procID())
                    {
                        //MotionItem* fromMotionItem = new MotionItem(*siter, di, bxSrc, bxDest);
                        MotionItem fromMotionItem(*siter, di, bxSrc, bxDest);
                        fromMotionItem.procID =destProcID;
                        this->m_fromMotionPlan.push_back(fromMotionItem);
                    }
                }
            }
        }
    } // end src loop
    this->sort();  
}

template<typename T, unsigned int C, MemType MEM, Centering CTR>
void
LevelExchangeCopier<T, C, MEM, CTR>::buildMotionPlans(LevelCopierOp<T, C, MEM, CTR>& a_op)
{
    auto layout = a_op.m_src->layout();
    auto ghost  = a_op.m_src->ghost();

    int myProcID = Proto::procID();
    for (auto dit = layout.begin(); dit.ok(); ++dit)
    {
        Box bx0 = dit.box();
        Box bx = layout.domain() & dit.box().grow(ghost);
        NeighborIterator nit(layout, bx);
        for (nit.begin(); nit.ok(); ++nit)
        {
            int nbrProcID = nit.procID();
            Point nbrShift = nit.shift();
            // For each pair of interacting boxes, we construct a
            // Motion to, from, and local.
            // Compute the boxes corresponding to filling from *dit to *nit
            // The source box is shifted, the dest box is not.
            
            Box destBx = nit.destBox();
            if (!destBx.empty() && !bx0.contains(destBx))
            {
                Box srcBx = nit.srcBox();
                //MotionItem* toMotionItem = new MotionItem(*nit, *dit, srcBx, destBx);
                MotionItem toMotionItem(*nit, *dit, srcBx, destBx);
                if (nbrProcID == myProcID)
                {
                    // local motion
                    this->m_localMotionPlan.push_back(toMotionItem);
                } else {
                    toMotionItem.procID = nbrProcID;
                    this->m_toMotionPlan.push_back(toMotionItem);
                }
            }
            
            // Construct the motion for filling the ghost region for *nit from *dit

            if (nbrProcID != myProcID)
            {
                Box nbrbx = Box(nit.point(), nit.point()).refine(layout.boxSize()).grow(ghost);
                Box nbrbxShift = nbrbx.shift(-nbrShift);
                Box srcFromBx = nbrbxShift & bx0;
                Box destFromBx = srcFromBx.shift(nbrShift);
                MotionItem fromMotionItem(*dit, *nit, srcFromBx, destFromBx);
                fromMotionItem.procID = nbrProcID;
                this->m_fromMotionPlan.push_back(fromMotionItem);
            }
        } // end neighbor loop
    } // end patch loop
    this->sort();
}

template<typename T, unsigned int C, MemType MEM, Centering CTR>
void interpBoundaries(
        LevelBoxData<T, C, MEM, CTR>& a_crse,
        LevelBoxData<T, C, MEM, CTR>& a_fine,
        InterpStencil<T>&       a_interp)
{
    //FIXME: This implementation doesn't address refined regions directly
    //       adjacent to domain boundaries. -CLG
    Point refRatio = a_interp.ratio();
    auto  cfLayout = a_fine.layout().coarsen(refRatio);
    Point cfGhost = a_fine.ghost() / refRatio + Point::Ones();
    Point interpGhost = a_interp.ghost();
    LevelBoxData<T, C, MEM, CTR> temp(cfLayout, cfGhost + interpGhost);
    interpBoundaries(a_crse, a_fine, temp, a_interp);
}

template<typename T, unsigned int C, MemType MEM, Centering CTR>
void interpBoundaries(
        LevelBoxData<T, C, MEM, CTR>& a_crse,
        LevelBoxData<T, C, MEM, CTR>& a_fine,
        LevelBoxData<T, C, MEM, CTR>& a_crseFine,
        InterpStencil<T>&       a_interp)
{
    a_crse.exchange();
    a_crse.copyTo(a_crseFine);
    for (auto iter = a_fine.layout().begin(); iter.ok(); ++iter)
    {
        auto& fine_i = a_fine[*iter];
        auto& crse_i = a_crseFine[*iter];
        BoxData<T, C, MEM> fine_0(iter.box());
        fine_i.copyTo(fine_0);  // save a copy of non-interior data
        fine_i |= a_interp(crse_i);
        fine_0.copyTo(fine_i);
        // This method seems like a shortcut / hack, but each interpolation call
        // results in refRatio^DIM stencil applies, each of which is a kernel
        // launch for GPU targets. I suspect that two BoxData::copyTo calls
        // are faster, but this should be tested. -CLG
    }
    a_fine.exchange();
}

template<typename T, unsigned int C, MemType MEM, Centering CTR>
void averageDown(
        LevelBoxData<T, C, MEM, CTR>& a_crse,
        LevelBoxData<T, C, MEM, CTR>& a_fine,
        Point                         a_refRatio)
{
    auto  cfLayout = a_fine.layout().coarsen(a_refRatio);
    LevelBoxData<T, C, MEM, CTR> temp(cfLayout, Point::Zeros());
    averageDown(a_crse, a_fine, temp, a_refRatio);
}

template<typename T, unsigned int C, MemType MEM, Centering CTR>
void averageDown(
        LevelBoxData<T, C, MEM, CTR>& a_crse,
        LevelBoxData<T, C, MEM, CTR>& a_fine,
        int                           a_refRatio)
{
    averageDown(a_crse, a_fine, Point::Ones(a_refRatio));
}

template<typename T, unsigned int C, MemType MEM, Centering CTR>
void averageDown(
        LevelBoxData<T, C, MEM, CTR>& a_crse,
        LevelBoxData<T, C, MEM, CTR>& a_fine,
        LevelBoxData<T, C, MEM, CTR>& a_crseFine,
        Point                         a_refRatio)
{
    a_fine.exchange();
    auto AVG = Stencil<T>::AvgDown(a_refRatio);
    for (auto iter = a_fine.begin(); iter.ok(); ++iter)
    {
        auto& fine_i = a_fine[*iter];
        auto& crse_i = a_crseFine[*iter];
        crse_i |= AVG(fine_i);
    }
    a_crseFine.copyTo(a_crse);
}

template<typename T, unsigned int C, MemType MEM, Centering CTR>
void averageDown(
        LevelBoxData<T, C, MEM, CTR>& a_crse,
        LevelBoxData<T, C, MEM, CTR>& a_fine,
        LevelBoxData<T, C, MEM, CTR>& a_crseFine,
        int                           a_refRatio)
{
    averageDown(a_crse, a_fine, a_crseFine, Point::Ones(a_refRatio));
}

template<typename T, unsigned int C, MemType MEM, Centering CTR>
template<typename _T, unsigned int _C, MemType _MEM, Centering _CTR>
bool LevelBoxData<T, C, MEM, CTR>::compatible(const LevelBoxData<_T, _C, _MEM, _CTR>& a_data) const
{
    return m_layout.compatible(a_data.layout());
}

template<typename T, unsigned int C, MemType MEM, Centering CTR>
bool LevelBoxData<T, C, MEM, CTR>::compatible(const DisjointBoxLayout& a_layout) const
{
    return m_layout.compatible(a_layout);
}

template<typename T, unsigned int C, MemType MEM, Centering CTR>
bool LevelBoxData<T, C, MEM, CTR>::compatible(const DataIterator& a_iter) const
{
    return m_layout.compatible(a_iter);
}

template<typename T, unsigned int C, MemType MEM, Centering CTR>
bool LevelBoxData<T, C, MEM, CTR>::compatible(const DataIndex& a_index) const
{
    return m_layout.compatible(a_index);
}