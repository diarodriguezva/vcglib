#include <vector>
#include <vcg/complex/trimesh/subset.h>
#include <vcg/simplex/face/jumping_pos.h>
#include <vcg/space/planar_polygon_tessellation.h>

/** BIT-QUAD creation support:
    a few basic operations to work with bit-quads simplices
    (quads defined by faux edges over a tri mesh backbone)


   [ basic operations: ]

   bool IsDoublet(const Face& f, int wedge)
   void RemoveDoublet(Face &f, int wedge, Mesh& m)
    - identifies and removed "Doublets" (pair of quads sharing two consecutive edges)

   bool IsSinglet(const Face& f, int wedge)
   void RemoveSinglet(Face &f, int wedge, Mesh& m)

   void FlipBitQuadDiag(Face &f)
    - rotates the faux edge of a quad (quad only change internally)

   bool RotateBitQuadEdge(Face& f, int w0a);
    - rotate a quad edge (clockwise or counterclockwise, specified via template)
   
   bool RotateBitQuadVertex(FaceType &f, int w0)
    - rotate around a quad vertex ("wind-mill" operation)

   void CollapseQuadDiag(Face &f, ... p , Mesh& m)
    - collapses a quad on its diagonal. 
    - p identifies the pos of collapsed point 
      (as either the parametric pos on the diagonal, or a fresh coordtype)
     

   [ helper functions: ]

   ScalarType quadQuality( ... ); 
    - returns the quality for a given quad 
    - (should be made into a template parameter for methods using it)
    - currently measures how squared each angle is
   
   int FauxIndex(const Face* f);
    - returns index of the only faux edge of a quad (otherwise, assert)

   int CountBitPolygonInternalValency(const Face& f, int wedge)
    - returns valency of vertex in terms  of polygons (quads, tris...)
    
  
*/

// this must become a parameter in the corresponding class
#define DELETE_VERTICES 0
// let's not remove them after all...
// since TwoManyfold is weak, the vertex could still be used elsewhere...

namespace vcg{namespace tri{

// helper function:
// cos of angle abc. This should probably go elsewhere
template<class CoordType>
static typename CoordType::ScalarType Cos(const CoordType &a, const CoordType &b, const CoordType &c )
{
  CoordType 
    e0 = b - a,
    e1 = b - c;
  typename CoordType::ScalarType d =  (e0.Norm()*e1.Norm());
  if (d==0) return 0.0;
  return (e0*e1)/d;
}

// helper function:
// returns quality of a quad formed by points a,b,c,d
// quality is computed as "how squared angles are"
template <class Coord>
inline static typename Coord::ScalarType quadQuality(const Coord &a, const Coord &b, const Coord &c, const Coord &d){
  typename Coord::ScalarType score = 0;
  score += 1 - math::Abs( Cos( a,b,c) );
  score += 1 - math::Abs( Cos( b,c,d) );
  score += 1 - math::Abs( Cos( c,d,a) );
  score += 1 - math::Abs( Cos( d,a,b) );
  return score / 4;
}

// helper function:
// returns quality of a given (potential) quad
template <class Face>
static typename Face::ScalarType quadQuality(Face *f, int edge){
  
  typedef typename Face::CoordType CoordType;
  
  CoordType 
    a = f->V0(edge)->P(),
    b = f->FFp(edge)->V2( f->FFi(edge) )->P(),
    c = f->V1(edge)->P(),
    d = f->V2(edge)->P();
  
  return quadQuality(a,b,c,d);

}

/**
helper function:
given a quad edge, retruns:
   0 if that edge should not be rotated
  +1 if it should be rotated clockwise (+1)
  -1 if it should be rotated counterclockwise (-1)
Currently an edge is rotated iff it is shortened by that rotations
(shortcut criterion)
*/
template <class Face>
int TestBitQuadEdgeRotation(const Face &f, int w0)
{
  const Face *fa = &f;
  assert(! fa->IsF(w0) );
  typename Face::ScalarType q0,q1,q2;
  typename Face::CoordType v0,v1,v2,v3,v4,v5;
  int w1 = (w0+1)%3;
  int w2 = (w0+2)%3;
  
  v0 = fa->P(w0);
  v3 = fa->P(w1);
  
  if (fa->IsF(w2) ) {
    v1 = fa->cFFp(w2)->V2( fa->cFFi(w2) )->P();
    v2 = fa->P(w2);
  } else {
    v1 = fa->P(w2);
    v2 = fa->cFFp(w1)->V2( fa->cFFi(w1) )->P();
  }
  
  const Face *fb = fa->cFFp(w0);
  w0 = fa->cFFi(w0);
  
  w1 = (w0+1)%3;
  w2 = (w0+2)%3;
  if (fb->IsF(w2) ) {
    v4 = fb->cFFp(w2)->V2( fb->cFFi(w2) )->P();
    v5 = fb->P(w2);
  } else {
    v4 = fb->P(w2);
    v5 = fb->cFFp(w1)->V2( fb->cFFi(w1) )->P();
  }
  
  /*
  //  max overall quality criterion:
  q0 = quadQuality(v0,v1,v2,v3) +  quadQuality(v3,v4,v5,v0); // keep as is?
  q1 = quadQuality(v1,v2,v3,v4) +  quadQuality(v4,v5,v0,v1); // rotate CW?
  q2 = quadQuality(v5,v0,v1,v2) +  quadQuality(v2,v3,v4,v5); // rotate CCW?
  
  if (q0>=q1 && q0>=q2) return 0;
  if (q1>=q2) return 1;*/
  
  // min distance (shortcut criterion)
  q0 = (v0 - v3).SquaredNorm();
  q1 = (v1 - v4).SquaredNorm();
  q2 = (v5 - v2).SquaredNorm();
  if (q0<=q1 && q0<=q2) return 0;
  if (q1<=q2) return 1;
  return -1;
}



template <class Face, bool verse>
bool RotateBitQuadEdge(Face& f, int w0a){
  Face *fa = &f;
  assert(! fa->IsF(w0a) );

  typename Face::VertexType *v0, *v1;
  v0= fa->V0(w0a);
  v1= fa->V1(w0a);
  
  int w1a = (w0a+1)%3;
  int w2a = (w0a+2)%3;

  Face *fb = fa->FFp(w0a);
  int w0b = fa->FFi(w0a);
  int w1b = (w0b+1)%3;
  int w2b = (w0b+2)%3;
    
  if (fa->IsF(w2a) == verse) {
    if (!CheckFlipBitQuadDiag(*fa)) return false;
    FlipBitQuadDiag(*fa);
    // recover edge index, so that (f, w0a) identifies the same edge as before
    Face *fc = fa->FFp(FauxIndex(fa));
    for (int i=0; i<3; i++){
      if ( v0==fa->V0(i) && v1==fa->V1(i) ) w0a = i;
      if ( v0==fc->V0(i) && v1==fc->V1(i) ) { fa = fc; w0a = i; }
    }
  }
  
  if (fb->IsF(w2b) == verse) {
    if (!CheckFlipBitQuadDiag(*fb)) return false;
    FlipBitQuadDiag(*fb);
  }
  
  if (!CheckFlipEdge(*fa,w0a)) return false;
  FlipBitQuadEdge(*fa,w0a);
  return true;
}

/* small helper function which returns the index of the only
   faux index, assuming there is exactly one (asserts out otherwise)
*/
template <class Face>
int FauxIndex(const Face* f){
  if (f->IsF(0)) return 0;
  if (f->IsF(1)) return 1;
  assert(f->IsF(2));
  return 2;
}

// rotates the diagonal of a quad
template <class Face>
void FlipBitQuadDiag(Face &f){
  int faux = FauxIndex(&f);
  Face* fa = &f;
  Face* fb = f.FFp(faux);
  vcg::face::FlipEdge(f, faux);
  // ripristinate faux flags
  fb->ClearAllF();
  fa->ClearAllF();
  for (int k=0; k<3; k++) {
    if (fa->FFp(k) == fb) fa->SetF(k);
    if (fb->FFp(k) == fa) fb->SetF(k);
  }
}


// given a vertex (i.e. a face and a wedge), 
// this function tells us how the average edge lenght around a vertex would change
// if that vertex is rotated
template <class Face>
typename Face::ScalarType AvgBitQuadEdgeLenghtVariationIfVertexRotated(const Face &f, int w0)
{
  assert(!f.IsD());
  typedef typename Face::ScalarType ScalarType;
  
  ScalarType 
    before=0, // sum of quad edges (originating from v)
    after=0;  // sum of quad diag (orginating from v)
  int guard = 0;

  // rotate arond vertex
  const Face* pf = &f;
  int pi = w0;
  int n = 0; // vertex valency
  int na = 0; 
  do {
    ScalarType triEdge = (pf->P0(pi) - pf->P1(pi) ).Norm();
    if (pf->IsF(pi)) { after += triEdge; na++;}
    else { before+= triEdge; n++; }
    if ( pf->IsF((pi+1)%3)) { after += CounterDiag( pf ).Norm(); na++; }
    
    const Face *t = pf;
    t = pf->FFp( pi );
    if (pf == t ) return std::numeric_limits<ScalarType>::max(); // it's a mesh border! flee!
    pi = pf->cFFi( pi );
    pi = (pi+1)%3; // Face::Next( pf->FFi( pi ) );
    pf = t;
    assert(guard++<100);
  } while (pf != &f);
  assert (na == n);
  return (after-before)/n;
}

/*
  const Face* pf = &f;
  int pi = wedge;
  int res = 0, guard=0;
  do {
    if (!pf->IsAnyF()) return false; // there's a triangle!
    if (!pf->IsF(pi)) res++;
    const Face *t = pf;
    t = pf->FFp( pi );
    if (pf == t ) return false;
    pi = pf->cFFi( pi );
    pi = (pi+1)%3; // Face::Next( pf->FFi( pi ) );
    pf = t;
    assert(guard++<100);
  } while (pf != &f);
*/

// given a vertex (i.e. a face and a wedge), 
// this function tells us if it should be rotated or not
// (currently, we should iff it is shortened)
template <class Face>
bool TestBitQuadVertexRotation(const Face &f, int w0)
{
  assert(!f.IsD());
  // rotate quad IFF this way edges become shorter:
  return AvgBitQuadEdgeLenghtVariationIfVertexRotated(f,w0)<0;
}


template <class FaceType>
bool RotateBitQuadVertex(FaceType &f, int w0)
{
  typedef typename FaceType::ScalarType ScalarType;
  
  int guard = 0;

  FaceType* pf = &f;
  int pi = w0;
  int n = 0; // vertex valency
  
  if (pf->IsF((pi+2) % 3)) { 
      pi = (pi+2)%3;
      // do one step back
      int tmp = pf->FFi(pi); pf = pf->FFp(pi); pi = tmp;  // flipF 
  }
  
  const FaceType* stopA = pf;
  const FaceType* stopB = pf->FFp(FauxIndex(pf));

  // rotate around vertex, flipping diagonals if necessary,
  do {
    bool mustFlip;
    if (pf->IsF(pi)) { 
      // if next edge is faux, move on other side of quad
      int tmp = (pf->FFi(pi)+1)%3; pf = pf->FFp(pi); pi = tmp;  // flipF 
      mustFlip = false;
    }
    else {
      mustFlip = true;
    }

    FaceType *lastF = pf;
    
    int tmp = (pf->FFi(pi)+1)%3; pf = pf->FFp(pi); pi = tmp;  // flipF
        
    if (mustFlip) {
      if (!CheckFlipBitQuadDiag(*lastF)) return false; // cannot flip??
      FlipBitQuadDiag(*lastF);
    }
    
  } while (pf != stopA && pf!= stopB);
  
  // last pass: rotate arund vertex again, changing faux status
  stopA=pf;
  do {
    int j = pi;
    if (pf->IsF(j)) pf->ClearF(j); else pf->SetF(j);
    j = (j+2)%3;
    if (pf->IsF(j)) pf->ClearF(j); else pf->SetF(j);
    int tmp = (pf->FFi(pi)+1)%3; pf = pf->FFp(pi); pi = tmp;  // flipF flipV
  } while (pf != stopA );

  return true;
}





// flips the faux edge of a quad
template <class Face>
void FlipBitQuadEdge(Face &f, int k){
  assert(!f.IsF(k));
  Face* fa = &f;
  Face* fb = f.FFp(k);
  assert(fa!=fb); // else, rotating a border edge

  // backup prev other-quads-halves
  Face* fa2 = fa->FFp( FauxIndex(fa) );
  Face* fb2 = fb->FFp( FauxIndex(fb) );

  vcg::face::FlipEdge(*fa, k);
  
  // ripristinate faux flags
  fb->ClearAllF();
  fa->ClearAllF();
  for (int k=0; k<3; k++) {
    //if (fa->FFp(k) == fa2) fa->SetF(k);
    //if (fb->FFp(k) == fb2) fb->SetF(k);
    if (fa->FFp(k)->IsF( fa->FFi(k) )) fa->SetF(k);
    if (fb->FFp(k)->IsF( fb->FFi(k) )) fb->SetF(k);
  }
}

// check if a quad diagonal can be topologically flipped
template <class Face>
bool CheckFlipBitQuadDiag(Face &f){
  return (vcg::face::CheckFlipEdge(f, FauxIndex(&f) ) );
}

// given a face (part of a quad), returns its diagonal
template <class Face>
typename Face::CoordType Diag(const Face* f){
  int i = FauxIndex(f);
  return f->P1( i ) - f->P0( i );
}


// given a face (part of a quad), returns other diagonal
template <class Face>
typename Face::CoordType CounterDiag(const Face* f){
  int i = FauxIndex(f);
  return f->cP2( i ) - f->cFFp( i )->cP2(f->cFFi(i) ) ;
}

/* helper function:
   collapses a single face along its faux edge. 
   Updates FF adj of other edges. */
template <class Mesh>
void _CollapseQuadDiagHalf(typename Mesh::FaceType &f, int faux, Mesh& m)
{
  typedef typename Mesh::FaceType Face;
  int faux1 = (faux+1)%3;
  int faux2 = (faux+2)%3;
  
  Face* fA = f.FFp( faux1 );
  Face* fB = f.FFp( faux2 );
  int iA = f.FFi( faux1 );
  int iB = f.FFi( faux2 );
  
  if (fA==&f && fB==&f) { 
    // both non-faux edges are borders: tri-face disappears, just remove the vertex
    if (DELETE_VERTICES)
      Allocator<Mesh>::DeleteVertex(m,*(f.V(faux2))); 
  } else {
    if (fA==&f) {
      fB->FFp(iB) = fB;  fB->FFi(iB) = iB;
    } else {
      fB->FFp(iB) = fA;  fB->FFi(iB) = iA;
    }

    if (fB==&f) {
      fA->FFp(iA) = fA;  fA->FFi(iA) = iA;
    } else {
      fA->FFp(iA) = fB;  fA->FFi(iA) = iB;
    }
  }
  
  Allocator<Mesh>::DeleteFace(m,f);

}

template <class Mesh>
void RemoveDoublet(typename Mesh::FaceType &f, int wedge, Mesh& m){
  if (f.IsF((wedge+1)%3) ) {
    typename Mesh::VertexType *v = f.V(wedge);
    FlipBitQuadDiag(f);
    // quick hack: recover wedge index after flip
    if (f.V(0)==v) wedge = 0;
    else if (f.V(1)==v) wedge = 1;
    else {
      assert(f.V(2)==v);
      wedge = 2;
    }
  }
  typename Mesh::ScalarType k=(f.IsF(wedge))?1:0;
  CollapseQuadDiag(f, k, m);
  typename Mesh::VertexType *v = f.V(wedge);
}

template <class Mesh>
void RemoveSinglet(typename Mesh::FaceType &f, int wedge, Mesh& m){
  typename Mesh::FaceType *fa, *fb; // these will die
  typename Mesh::FaceType *fc, *fd; // their former neight
  fa = & f;
  fb = fa->FFp(wedge);
  int wa0 = wedge;
  int wa1 = (wa0+1)%3 ;
  int wa2 = (wa0+2)%3 ;
  int wb0 = (fa->FFi(wa0)+1)%3;
  int wb1 = (wb0+1)%3 ; 
  int wb2 = (wb0+2)%3 ;
  assert (fb == fa->FFp( wa2 ) ); // otherwise, not a singlet
  fc = fa->FFp(wa1);
  fd = fb->FFp(wb1);
  int wc = fa->FFi(wa1);
  int wd = fb->FFi(wb1);
  fc->FFp(wc) = fd;
  fc->FFi(wc) = wd;
  fd->FFp(wd) = fc;
  fd->FFi(wd) = wc;
  // faux status of survivors: unchanged
  assert( ! ( fc->IsF( wc) ) );
  assert( ! ( fd->IsF( wd) ) );
  Allocator<Mesh>::DeleteFace( m,*fa );
  Allocator<Mesh>::DeleteFace( m,*fb );
  if (DELETE_VERTICES)
  Allocator<Mesh>::DeleteVertex( m,*fa->V(wedge) );
}


template <class Mesh>
bool TestAndRemoveDoublet(typename Mesh::FaceType &f, int wedge, Mesh& m){
  if (IsDoublet(f,wedge)) {
     RemoveDoublet(f,wedge,m);
     return true;
  }
  return false;
}

template <class Mesh>
bool TestAndRemoveSinglet(typename Mesh::FaceType &f, int wedge, Mesh& m){
  if (IsSinglet(f,wedge)) {
     RemoveSinglet(f,wedge,m);
     return true;
  }
  return false;
}
template <class Face, int verse>
void RotateBitQuadEdge(const Face& f, int wedge){
}

// given a face and a wedge, counts its valency in terms of quads (and triangles)
// uses only FF, assumes twomanyfold
// returns -1 if border
template <class Face>
int CountBitPolygonInternalValency(const Face& f, int wedge){
  const Face* pf = &f;
  int pi = wedge;
  int res = 0;
  do {
    if (!pf->IsF(pi)) res++;
    const Face *t = pf;
    t = pf->FFp( pi );
    if (pf == t ) return -1;
    pi = (pi+1)%3; // Face::Next( pf->FFi( pi ) );
    pf = t;
  } while (pf != &f);
  return res;
}

// given a face and a wedge, returns if it host a doubet
// assumes tri and quad only. uses FF topology only.
template <class Face>
bool IsDoublet(const Face& f, int wedge){
  const Face* pf = &f;
  int pi = wedge;
  int res = 0, guard=0;
  do {
    if (!pf->IsAnyF()) return false; // there's a triangle!
    if (!pf->IsF(pi)) res++;
    const Face *t = pf;
    t = pf->FFp( pi );
    if (pf == t ) return false;
    pi = pf->cFFi( pi );
    pi = (pi+1)%3; // Face::Next( pf->FFi( pi ) );
    pf = t;
    assert(guard++<100);
  } while (pf != &f);
  return (res == 2);
}

template <class Face>
bool IsSinglet(const Face& f, int wedge){
  const Face* pf = &f;
  int pi = wedge;
  int res = 0, guard=0;
  do {
    if (!pf->IsAnyF()) return false; // there's a triangle!
    if (!pf->IsF(pi)) res++;
    const Face *t = pf;
    t = pf->FFp( pi );
    if (pf == t ) return false;
    pi = pf->cFFi( pi );
    pi = (pi+1)%3; // Face::Next( pf->FFi( pi ) );
    pf = t;
    assert(guard++<100);
  } while (pf != &f);
  return (res == 1);
}



/** collapses a quad diagonal a-b 
  forming the new vertex in between the two old vertices.
   if k == 0, new vertex is in a
   if k == 1, new vertex is in b
   if k == 0.5, new vertex in the middle, etc
*/
template <class Mesh>
void CollapseQuadDiag(typename Mesh::FaceType &f, typename Mesh::ScalarType k, Mesh& m){
  typename Mesh::CoordType p;
  int fauxa = FauxIndex(&f);
  p = f.V(fauxa)->P()*(1-k) + f.V( (fauxa+1)%3 )->P()*(k);
  CollapseQuadDiag(f,p,m);
}

template <class Mesh>
bool CollapseQuadEdgeDirect(typename Mesh::FaceType &f, int w0, Mesh& m){
  typename Mesh::FaceType * f0 = &f;

  assert( !f0->IsF(w0) );
  
  typename Mesh::VertexType *v0, *v1;
  v0 = f0->V0(w0);
  v1 = f0->V1(w0);
  
  if (!RotateBitQuadVertex(*f0,w0)) return false;
  
  
  // quick hack: recover original wedge
  if      (f0->V(0) == v0) w0 = 0;
  else if (f0->V(1) == v0) w0 = 1;
  else if (f0->V(2) == v0) w0 = 2; 
  else assert(0);
  
  assert( f0->V1(w0) == v1 );
  assert( f0->IsF(w0) );
  
  CollapseQuadDiag(*f0,PosOnDiag(*f0,false), m);
  return true;
}

template <class Mesh>
bool CollapseQuadEdge(typename Mesh::FaceType &f, int w0, Mesh& m){
  typedef typename Mesh::FaceType * FaceTypeP;
  FaceTypeP f0 = &f;
  
  assert(!f0->IsF(w0)); // don't use this to collapse diag.
  
  FaceTypeP f1 = f0->FFp(w0); 
  int w1 = f0->FFi(w0); 
  
  assert(f0!=f1); // can't collapse border edges!
  
  // choose: rotate around V0 or around V1?
  if (
    AvgBitQuadEdgeLenghtVariationIfVertexRotated(*f0,w0)
    <
    AvgBitQuadEdgeLenghtVariationIfVertexRotated(*f1,w1)
  )    return CollapseQuadEdgeDirect(*f0,w0,m); 
  else return CollapseQuadEdgeDirect(*f1,w1,m);
}


template <class Mesh>
void CollapseQuadDiag(typename Mesh::FaceType &f, const typename Mesh::CoordType &p, Mesh& m){
  
  typedef typename Mesh::FaceType Face;
  typedef typename Mesh::VertexType Vert;
  
  Face* fa = &f;
  int fauxa = FauxIndex(fa);
  Face* fb = fa->FFp(fauxa);
  assert (fb!=fa);
  int fauxb = FauxIndex(fb);
  
  Vert* va = fa->V(fauxa); // va lives
  Vert* vb = fb->V(fauxb); // vb dies
  
  // update FV...
  bool border = false;
  int pi = fauxb;
  Face* pf = fb; /* pf, pi could be a Pos<Face> p(pf, pi) */
  // rotate around vb, (same-sense-as-face)-wise
  do {
    pf->V(pi) = va;
    
    pi=(pi+2)%3;
    Face *t = pf->FFp(pi);
    if (t==pf) { border= true; break; }
    pi = pf->FFi(pi);
    pf = t;
  } while (pf!=fb);
  
  // rotate around va, (counter-sense-as-face)-wise
  if (border) {
    int pi = fauxa;
    Face* pf = fa; /* pf, pi could be a Pos<Face> p(pf, pi) */
    do {
      pi=(pi+1)%3;
      pf->V(pi) = va;
      Face *t = pf->FFp(pi);
      if (t==pf) break;
      pi = pf->FFi(pi);
      pf = t;
    } while (pf!=fb);
  }
    
  // update FF, delete faces
  _CollapseQuadDiagHalf(*fb, fauxb, m);
  _CollapseQuadDiagHalf(*fa, fauxa, m);
  
  if (DELETE_VERTICES) Allocator<Mesh>::DeleteVertex(m,*vb);
  va->P() = p;
}      


template <class Mesh>
void CollapseQuadCounterDiag(typename Mesh::FaceType &f, typename Mesh::ScalarType k, Mesh& m){
  typename Mesh::CoordType p;
  int fauxa = FauxIndex(&f);
  p = f.P2(fauxa)*(1-k) + f.FFp( fauxa )->P2( f.FFi( fauxa ) )*(k);
  CollapseQuadCounterDiag(f,p,m);
}

template <class Mesh>
void CollapseQuadCounterDiag(typename Mesh::FaceType &f, const typename Mesh::CoordType &p, Mesh& m){
  FlipBitQuadDiag(f);
  CollapseQuadDiag(f,p,m);
}


// helper function: find a good position on a diag to collapse a point
// currently, it is point in the middle, 
//    unless a mixed border-non border edge is collapsed, then it is an exreme
template <class Face>
typename Face::ScalarType PosOnDiag(const Face& f, bool counterDiag){
  bool b0, b1, b2, b3; // which side of the quads are border
  
  const Face* fa=&f;
  int ia = FauxIndex(fa);
  const Face* fb=fa->cFFp(ia); 
  int ib = fa->cFFi(ia);
  
  b0 = fa->FFp((ia+1)%3) == fa;
  b1 = fa->FFp((ia+2)%3) == fa;
  b2 = fb->FFp((ib+1)%3) == fb;
  b3 = fb->FFp((ib+2)%3) == fb;
  
  if (counterDiag) {
    if (  (b0||b1) && !(b2||b3) ) return 0;
    if ( !(b0||b1) &&  (b2||b3) ) return 1;
  } else {
    if (  (b1||b2) && !(b3||b0) ) return 0;
    if ( !(b1||b2) &&  (b3||b0) ) return 1;
  }
  //if (f->FF( FauxIndex(f) )->IsB(
  return 0.5f;
}

template <class Mesh>
void UpdateQualityAsBitQuadValency(Mesh& m){
  typedef typename Mesh::FaceIterator FaceIterator;
  typedef typename Mesh::VertexIterator VertexIterator;
  for (VertexIterator vi = m.vert.begin();  vi!=m.vert.end(); vi++) if (!vi->IsD()) {
     vi->Q() = 0; 
  }

  for (FaceIterator fi = m.face.begin();  fi!=m.face.end(); fi++) if (!fi->IsD()) {
     for (int w=0; w<3; w++) 
       fi->V(w)->Q() += (fi->IsF(w)||fi->IsF((w+2)%3) )? 0.5f:1; 
  }
}

}} // end namespace vcg::tri