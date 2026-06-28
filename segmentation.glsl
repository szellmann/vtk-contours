//VTK::System::Dec
//VTK::Output::Dec
in vec2 tcoordVCVSOutput;
uniform sampler2D actortexture;
// data set
uniform ivec2 dims;
uniform vec2 range;
uniform sampler2D data;
// contour tree
uniform usampler2D nodes;
uniform usampler2D arcs;
uniform usampler2D superparents;
uniform usampler2D superarcs;
uniform usampler2D supernodes;
uniform usampler2D hyperparents;
uniform usampler2D whenTransferred;
uniform usampler2D hypernodes;
uniform usampler2D hyperarcs;
uniform uint numSupernodes;
uniform uint numHypernodes;

#define INT_MIN  -2147483648
#define INT_MAX   2147483647
#define UINT_MIN  0u
#define UINT_MAX  4294967295u

// ./filter/scalar_topology/worklet/contourtree_augmented//Types.h
#define NO_SUCH_ELEMENT 0x80000000u
#define IS_ASCENDING    0x08000000u
#define INDEX_MASK      0x07FFFFFFu
//constexpr viskores::Id NO_SUCH_ELEMENT = std::numeric_limits<viskores::Id>::min();
//constexpr viskores::Id TERMINAL_ELEMENT = std::numeric_limits<viskores::Id>::max() / 2 + 1; //0x40000000 || 0x4000000000000000
//constexpr viskores::Id IS_SUPERNODE = std::numeric_limits<viskores::Id>::max() / 4 + 1; //0x20000000 || 0x2000000000000000
//constexpr viskores::Id IS_HYPERNODE = std::numeric_limits<viskores::Id>::max() / 8 + 1; //0x10000000 || 0x1000000000000000
//constexpr viskores::Id IS_ASCENDING = std::numeric_limits<viskores::Id>::max() / 16 + 1; //0x08000000 || 0x0800000000000000
//constexpr viskores::Id INDEX_MASK = std::numeric_limits<viskores::Id>::max() / 16; //0x07FFFFFF || 0x07FFFFFFFFFFFFFF
//constexpr viskores::Id CV_OTHER_FLAG = std::numeric_limits<viskores::Id>::max() / 8 + 1; //0x10000000 || 0x1000000000000000
//constexpr viskores::Id ELEMENT_EXISTS = std::numeric_limits<viskores::Id>::max() / 4 + 1; //0x20000000 || 0x2000000000000000 , same as IS_SUPERNODE

vec3 randomColor(uint idx) {
  uint r = uint(int(idx)*13*17 + 0x234235);
  uint g = uint(int(idx)*7*3*5 + 0x773477);
  uint b = uint(int(idx)*11*19 + 0x223766);
  return vec3((r&255u)/255.f,
              (g&255u)/255.f,
              (b&255u)/255.f);
}

// 32-bit index from 64-bit ID
uint maskedIndex(uint id) {
  return id & INDEX_MASK;
}

bool noSuchElement(uint id) {
  return (id & NO_SUCH_ELEMENT) != 0u;
}

bool isAscending(uint id) {
  return (id & IS_ASCENDING) != 0u;
}

uint getID(usampler2D samp, uint index) {
  uint i=maskedIndex(index)%4096u;
  uint j=maskedIndex(index)/4096u;
  uvec4 ui4 = texelFetch(samp, ivec2(int(i),int(j)), 0);
  return ui4[index%4u];
}

uint getID(usampler2D samp, int index) {
  return getID(samp, uint(index));
}

float getDataByRegularID(uint regID) {
  uint i=maskedIndex(regID)%4096u;
  uint j=maskedIndex(regID)/4096u;
  vec4 f4 = texelFetch(data, ivec2(int(i),int(j)), 0);
  return f4.r;
}

float getDataBySuperID(uint superID) {
  uint regID = getID(supernodes, superID);
  return getDataByRegularID(regID);
}


// a vertex structure given both its value _and_ its address
// so we can compare using simulation of simplicity
// https://arxiv.org/pdf/math/9410209
struct MeshVertex {
  uint addr;
  float value;
};

bool compLT(MeshVertex a, MeshVertex b) {
  if (a.value < b.value)
    return true;
  else if (a.value == b.value)
    return a.addr < b.addr;
  else
    return false;
}

bool compGT(MeshVertex a, MeshVertex b) {
  if (a.value > b.value)
    return true;
  else if (a.value == b.value)
    return a.addr > b.addr;
  else
    return false;
}

float bary(MeshVertex a, MeshVertex b, MeshVertex c, float u, float v) {
  float s2 = c.value*v;
  float s3 = b.value*u;
  float s1 = a.value*(1.0f-(u+v));
  return s1+s2+s3;
}

void main() {
  vec4 texColor = texture(actortexture, tcoordVCVSOutput);
  gl_FragData[0] = texColor;

  // compute the triangle we're in
  vec2 uv = tcoordVCVSOutput.xy;
  int x = int(uv.x*float(dims.x-1));
  int y = int(uv.y*float(dims.y-1));
  float xf = float(uv.x*float(dims.x-1));
  float yf = float(uv.y*float(dims.y-1));
  float xfrac = xf-float(x);
  float yfrac = yf-float(y);

  int p0 = x+y*dims.x;
  int p1 = (x+1)+y*dims.x;
  int p2 = (x+1)+(y+1)*dims.x;
  int p3 = x+(y+1)*dims.x;

  MeshVertex t0[3], t1[3];
  t0[0] = MeshVertex(uint(p0),getDataByRegularID(uint(p0)));
  t0[1] = MeshVertex(uint(p1),getDataByRegularID(uint(p1)));
  t0[2] = MeshVertex(uint(p2),getDataByRegularID(uint(p2)));

  t1[0] = MeshVertex(uint(p0),getDataByRegularID(uint(p0)));
  t1[1] = MeshVertex(uint(p2),getDataByRegularID(uint(p2)));
  t1[2] = MeshVertex(uint(p3),getDataByRegularID(uint(p3)));

  // in the bottom/right triangle, u-coord "grows" faster,
  // in the top/left triangle, v-coord "grows" faster:
  int triID = (xfrac >= yfrac) ? 0 : 1;

  // function value of node on triangle surface
  float value = (triID==0) ? bary(t0[0],t0[1],t0[2],xfrac-yfrac,yfrac)
                           : bary(t1[0],t1[1],t1[2],xfrac,yfrac-xfrac);

  MeshVertex bottomVertex = MeshVertex(NO_SUCH_ELEMENT,1e31f);
  MeshVertex topVertex = MeshVertex(NO_SUCH_ELEMENT,-1e31f);

  // compute top and bottom ID (triangle vertices with max and min value):
  for (int i=0; i<3; ++i) {
    if (triID==0) {
      if (compLT(t0[i],bottomVertex)) bottomVertex = t0[i];
      if (compGT(t0[i],topVertex)) topVertex = t0[i];
    } else {
      if (compLT(t1[i],bottomVertex)) bottomVertex = t1[i];
      if (compGT(t1[i],topVertex)) topVertex = t1[i];
    }
  }

  uint bottom = bottomVertex.addr;
  uint top = topVertex.addr;

  MeshVertex node = MeshVertex((bottomVertex.addr+topVertex.addr)/2u,value);

  #define FATAL(X) if (X) { gl_FragData[0] = vec4(0.f); return; }

  // The superarc we search for:
  uint superparent = NO_SUCH_ELEMENT;

  // BEGIN LocateSuperarcs
  if (true) {
    // regular nodes only
    // we will need to prune top and bottom until one of them prunes past the node
    uint topSuperparent = getID(superparents, top);
    uint bottomSuperparent = getID(superparents, bottom);
    // and we can also find out when they transferred
    uint topWhen = getID(whenTransferred, topSuperparent);
    uint bottomWhen = getID(whenTransferred, bottomSuperparent);
    // and their hyperparent
    uint topHyperparent = getID(hyperparents, topSuperparent);
    uint bottomHyperparent = getID(hyperparents, bottomSuperparent);
    // our goal is to work out the true hyperparent of the node
    uint hyperparent = NO_SUCH_ELEMENT;

    // now we loop until one of them goes past the vertex
    // the invariant here is that the first direction to prune past the vertex prunes it
    while (noSuchElement(hyperparent)) {
      // loop to find pruner
      // we test the one that prunes first
      if (maskedIndex(topWhen) < maskedIndex(bottomWhen)) {
        // top pruned first
        // we prune down to the bottom of the hyperarc in either case, by updating the top superparent
        topSuperparent = getID(hyperarcs, topHyperparent);
        top = getID(supernodes, topSuperparent);

        topWhen = getID(whenTransferred, topSuperparent);
        // test to see if we've passed the node
        if (compLT(MeshVertex(maskedIndex(top), getDataByRegularID(top)), node)) {
          // just pruned past
          hyperparent = topHyperparent;
        } // just pruned past
        // == is not possible, since node is regular
        else // top < node
        {    // not pruned past
          FATAL (getID(hyperparents, topSuperparent) == topHyperparent)
          topHyperparent = getID(hyperparents, topSuperparent);
        } // not pruned past
      }   // top pruned first
      else if (maskedIndex(topWhen) > maskedIndex(bottomWhen)) {
        // bottom pruned first
        // we prune up to the top of the hyperarc in either case, by updating the bottom superparent
        bottomSuperparent = getID(hyperarcs, bottomHyperparent);
        bottom = getID(supernodes, bottomSuperparent);
        bottomWhen = getID(whenTransferred, bottomSuperparent);
        // test to see if we've passed the node
        if (compGT(MeshVertex(maskedIndex(bottom), getDataByRegularID(bottom)), node)) {
          // just pruned past
          hyperparent = bottomHyperparent;
        } // just pruned past
        // == is not possible, since node is regular
        else // bottom > node
        {    // not pruned past
          FATAL(getID(hyperparents, bottomSuperparent) == bottomHyperparent)
          bottomHyperparent = getID(hyperparents, bottomSuperparent);
        } // not pruned past
      }   // bottom pruned first
      else {
        // both prune simultaneously
        // this can happen when both top & bottom prune in the same pass because they belong to the same hyperarc
        // but this means that they must have the same hyperparent, so we know the correct hyperparent & can check whether it ascends
        hyperparent = bottomHyperparent;
      }
    }   // loop to find pruner
    if (isAscending(getID(hyperarcs, hyperparent))) {
      // ascending hyperarc
      // the supernodes on the hyperarc are in sorted low-high order
      uint lowSupernode = getID(hypernodes, hyperparent);
      uint highSupernode;
      // if it's at the right hand end, take the last supernode in the array
      if (maskedIndex(hyperparent) == numHypernodes - 1u)
        highSupernode = numSupernodes - 1u;
      // otherwise, take the supernode just before the next hypernode
      else
        highSupernode = getID(hypernodes, maskedIndex(hyperparent) + 1u) -1u;
      // now, the high supernode may be lower than the element, because the node belongs
      // between it and the high end of the hyperarc
      MeshVertex other = MeshVertex(getID(supernodes, highSupernode), getDataBySuperID(highSupernode));
      if (compLT(other, node))
        superparent = highSupernode;
      // otherwise, we do a binary search of the superarcs
      else {
        // node between high & low
        // keep going until we span exactly
        while (highSupernode - lowSupernode > 1u) {
          // binary search
          uint midSupernode = (lowSupernode + highSupernode) / 2u;
          // test against the node
          if (compGT(MeshVertex(getID(supernodes, midSupernode), getDataBySuperID(midSupernode)), node))
            highSupernode = midSupernode;
          // == can't happen since node is regular
          else
            lowSupernode = midSupernode;
        } // binary search

        // now we can use the low node as the superparent
        superparent = lowSupernode;
      } // node between high & low
    }   // ascending hyperarc 
    else {
      // descending hyperarc
      // the supernodes on the hyperarc are in sorted high-low order
      uint highSupernode = getID(hypernodes, hyperparent);
      uint lowSupernode;
      // if it's at the right hand end, take the last supernode in the array
      if (maskedIndex(hyperparent) == numHypernodes - 1u) {
        // last hyperarc
        lowSupernode = numSupernodes - 1u;
      } // other hyperarc
      // otherwise, take the supernode just before the next hypernode
      else {
        // other hyperarc
        lowSupernode = getID(hypernodes, maskedIndex(hyperparent) + 1u) - 1u;
      } // other hyperarc
      // now, the low supernode may be higher than the element, because the node belongs
      // between it and the low end of the hyperarc
      MeshVertex other = MeshVertex(getID(supernodes, lowSupernode), getDataBySuperID(lowSupernode));
      if (compGT(other, node))
        superparent = lowSupernode;
      // otherwise, we do a binary search of the superarcs
      else {
        // node between low & high
        // keep going until we span exactly
        while (lowSupernode - highSupernode > 1u) {
          // binary search
          // find the midway supernode
          uint midSupernode = (highSupernode + lowSupernode) / 2u;
          // test against the node
          if (compGT(MeshVertex(getID(supernodes, midSupernode), getDataBySuperID(midSupernode)), node))
            highSupernode = midSupernode;
          // == can't happen since node is regular
          else
            lowSupernode = midSupernode;
        } // binary search
        // now we can use the high node as the superparent
        superparent = highSupernode;
      }
    }
  }
  gl_FragData[0] = vec4(randomColor(superparent),1.f);

  //float f = (value-range.x)/(range.y-range.x);
  //gl_FragData[0] = vec4(vec3(f),1.f);
  //gl_FragData[0] = texColor;
}




