//VTK::System::Dec
//VTK::Output::Dec
in vec2 tcoordVCVSOutput;
uniform sampler2D actortexture;
// data set
uniform ivec2 dims;
uniform vec2 range;
uniform sampler2D data;
// contour tree
uniform usampler2D sortOrder;
uniform usampler2D sortIndices;
uniform usampler2D nodes;
uniform usampler2D arcs;
uniform usampler2D superparents;
uniform usampler2D superarcs;
uniform usampler2D supernodes;
uniform usampler2D hyperparents;
uniform usampler2D whenTransferred;
uniform usampler2D hypernodes;
uniform usampler2D hyperarcs;
uniform int numSupernodes;
uniform int numHypernodes;
// TF
uniform sampler2D rgbaLUT;
// user interaction
uniform vec2 uvSelected;
uniform int raster;

#define UINT_MIN  0u
#define UINT_MAX  4294967295u

// ./filter/scalar_topology/worklet/contourtree_augmented//Types.h
#define NO_SUCH_ELEMENT 0x80000000u
#define IS_ASCENDING    0x08000000u
#define INDEX_MASK      0x07FFFFFFu

vec3 randomColor(uint idx) {
  uint r = uint(int(idx)*13*17 + 0x234235);
  uint g = uint(int(idx)*7*3*5 + 0x773477);
  uint b = uint(int(idx)*11*19 + 0x223766);
  return vec3((r&255u)/255.f,
              (g&255u)/255.f,
              (b&255u)/255.f);
}

vec4 lut(uint index) {
  return texelFetch(rgbaLUT, ivec2(int(index),0), 0);
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
  uint i=index%4096u;
  uint j=index/4096u;
  uvec4 ui4 = texelFetch(samp, ivec2(int(i/4u),int(j)), 0);
  return ui4[index%4u];
}

float getDataValueByMeshID(uint meshID) {
  uint i=meshID%4096u;
  uint j=meshID/4096u;
  vec4 f4 = texelFetch(data, ivec2(int(i),int(j)), 0);
  return f4.r;
}

float getDataValueByRegularID(uint regID) {
  uint meshID = getID(sortOrder, regID);
  return getDataValueByMeshID(meshID);
}

float getDataValueBySuperID(uint superID) {
  uint regID = getID(supernodes, superID);
  return getDataValueByRegularID(regID);
}

float bary(float a, float b, float c, float u, float v) {
  float s2 = c*v;
  float s3 = b*u;
  float s1 = a*(1.0f-(u+v));
  return s1+s2+s3;
}

struct DataPoint {
  float addr;
  float value;
};

DataPoint bary(DataPoint a, DataPoint b, DataPoint c, float u, float v) {
  DataPoint dp;
  dp.addr  = bary(a.addr, b.addr, c.addr, u, v);
  dp.value = bary(a.value, b.value, c.value, u, v);
  return dp;
}

DataPoint makeDataPointFromMeshID(uint meshID) {
  DataPoint dp;
  dp.addr = float(meshID);
  dp.value = getDataValueByMeshID(meshID);
  return dp;
}

DataPoint makeDataPointFromRegularID(uint regID) {
  uint meshID = getID(sortOrder, regID);
  DataPoint dp;
  dp.addr = float(meshID);
  dp.value = getDataValueByRegularID(regID);
  return dp;
}

uint dataPointToRegularID(DataPoint dp) {
  uint meshID = uint(dp.addr);
  uint regID = getID(sortIndices, meshID);
  return regID;
}

bool lt(DataPoint a, DataPoint b) {
  if (a.value < b.value) return true;
  if (a.value > b.value) return false;
  if (a.addr < b.addr) return true;
  if (a.addr > b.addr) return false;
  return false;
}

bool gt(DataPoint a, DataPoint b) {
  if (a.value > b.value) return true;
  if (a.value < b.value) return false;
  if (a.addr > b.addr) return true;
  if (a.addr < b.addr) return false;
  return false;
}

uint locateSuperarc(vec2 uv) {
  // compute the triangle we're in
  int x = int(uv.x*float(dims.x-1));
  int y = int(uv.y*float(dims.y-1));
  float xf = float(uv.x*float(dims.x-1));
  float yf = float(uv.y*float(dims.y-1));
  float xfrac = xf-float(x);
  float yfrac = yf-float(y);

  // meshIDs
  int m00 = x+y*dims.x;
  int m10 = (x+1)+y*dims.x;
  int m11 = (x+1)+(y+1)*dims.x;
  int m01 = x+(y+1)*dims.x;

  // convert fragment to (continuous) regular ID interpolated
  // barycentrically over the triangle surface

  // convert triangle corners from mesh ID to regular ID:
  DataPoint t0[3];
  DataPoint t1[3];

  t0[0] = makeDataPointFromMeshID(uint(m00));
  t0[1] = makeDataPointFromMeshID(uint(m10));
  t0[2] = makeDataPointFromMeshID(uint(m11));

  t1[0] = makeDataPointFromMeshID(uint(m00));
  t1[1] = makeDataPointFromMeshID(uint(m11));
  t1[2] = makeDataPointFromMeshID(uint(m01));
#ifdef DBG
  std::cout << "m00: " << m00 << ", value: " << t0[0].value << '\n';
  std::cout << "m10: " << m10 << ", value: " << t0[1].value << '\n';
  std::cout << "m11: " << m11 << ", value: " << t0[2].value << '\n';
  std::cout << "m01: " << m01 << ", value: " << t1[2].value << '\n';

  std::cout << "Tri0:\n";
  std::cout << t0[0].value << ',' << t0[1].value << ',' << t0[2].value << '\n';
  std::cout << '\n';
  std::cout << "Tri1:\n";
  std::cout << t1[0].value << ',' << t1[1].value << ',' << t1[2].value << '\n';
  std::cout << '\n';
#endif

  // determine the triangle we are in:
  // in the bottom/right triangle, u-coord "grows" faster,
  // in the top/left triangle, v-coord "grows" faster:
  int triID = (xfrac >= yfrac) ? 0 : 1;

  DataPoint node = (triID==0) ? bary(t0[0],t0[1],t0[2],xfrac-yfrac,yfrac)
                              : bary(t1[0],t1[1],t1[2],xfrac,yfrac-xfrac);

  DataPoint bottom = DataPoint(1e30f, 1e30f);
  DataPoint top = DataPoint(-1e30f, -1e30f);

  // compute top and bottom ID (triangle vertices with max and min value):
  for (int i=0; i<3; ++i) {
    if (triID==0) {
      if (lt(t0[i], bottom)) bottom = t0[i];
      if (gt(t0[i], top)) top = t0[i];
    } else {
      if (lt(t1[i], bottom)) bottom = t1[i];
      if (gt(t1[i], top)) top = t1[i];
    }
  }
#ifdef DBG
  std::cout << "Node:\n";
  std::cout << node.addr << ',' << node.value << '\n';
  std::cout << '\n';
  std::cout << "top   : " << top.addr << ',' << top.value << '\n';
  std::cout << "bottom: " << bottom.addr << ',' << bottom.value << '\n';
#endif

  // The superarc we search for:
  uint superparent = NO_SUCH_ELEMENT;

  // we will need to prune top and bottom until one of them prunes past the node
  uint topSuperparent = getID(superparents, maskedIndex(dataPointToRegularID(top)));
  uint bottomSuperparent = getID(superparents, maskedIndex(dataPointToRegularID(bottom)));
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
      topSuperparent = getID(hyperarcs, maskedIndex(topHyperparent));
      uint topSuperparentRegularID = getID(supernodes, maskedIndex(topSuperparent));
      top = makeDataPointFromRegularID(topSuperparentRegularID);

      topWhen = getID(whenTransferred, maskedIndex(topSuperparent));
      // test to see if we've passed the node
      if (lt(top, node)) {
        // just pruned past
        hyperparent = topHyperparent;
      } // just pruned past
      // == is not possible, since node is regular
      else // top < node
      {    // not pruned past
        topHyperparent = getID(hyperparents, maskedIndex(topSuperparent));
      } // not pruned past
    }   // top pruned first
    else if (maskedIndex(topWhen) > maskedIndex(bottomWhen)) {
      // bottom pruned first
      // we prune up to the top of the hyperarc in either case, by updating the bottom superparent
      bottomSuperparent = getID(hyperarcs, maskedIndex(bottomHyperparent));
      uint bottomSuperparentRegularID = getID(supernodes, maskedIndex(bottomSuperparent));
      bottom = makeDataPointFromRegularID(bottomSuperparentRegularID);
      bottomWhen = getID(whenTransferred, maskedIndex(bottomSuperparent));
      // test to see if we've passed the node
      if (gt(bottom, node)) {
        // just pruned past
        hyperparent = bottomHyperparent;
      } // just pruned past
      // == is not possible, since node is regular
      else // bottom > node
      {    // not pruned past
        bottomHyperparent = getID(hyperparents, maskedIndex(bottomSuperparent));
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
    if (maskedIndex(hyperparent) == uint(numHypernodes) - 1u)
      highSupernode = uint(numSupernodes) - 1u;
    // otherwise, take the supernode just before the next hypernode
    else
      highSupernode = getID(hypernodes, maskedIndex(hyperparent) + 1u) -1u;
    // now, the high supernode may be lower than the element, because the node belongs
    // between it and the high end of the hyperarc
    uint highSupernodeRegularID = getID(supernodes, highSupernode);
    DataPoint highSupernodeDP = makeDataPointFromRegularID(highSupernodeRegularID);
    if (lt(highSupernodeDP, node))
      superparent = highSupernode;
    // otherwise, we do a binary search of the superarcs
    else {
      // node between high & low
      // keep going until we span exactly
      while (highSupernode - lowSupernode > 1u) {
        // binary search
        uint midSupernode = (lowSupernode + highSupernode) / 2u;
        // test against the node
        uint midSupernodeRegularID = getID(supernodes, midSupernode);
        DataPoint midSupernodeDP = makeDataPointFromRegularID(midSupernodeRegularID);
        if (gt(midSupernodeDP, node))
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
    if (maskedIndex(hyperparent) == uint(numHypernodes) - 1u) {
      // last hyperarc
      lowSupernode = uint(numSupernodes) - 1u;
    } // other hyperarc
    // otherwise, take the supernode just before the next hypernode
    else {
      // other hyperarc
      lowSupernode = getID(hypernodes, maskedIndex(hyperparent) + 1u) - 1u;
    } // other hyperarc
    // now, the low supernode may be higher than the element, because the node belongs
    // between it and the low end of the hyperarc
    uint lowSupernodeRegularID = getID(supernodes, lowSupernode);
    DataPoint lowSupernodeDP = makeDataPointFromRegularID(lowSupernodeRegularID);
    if (gt(lowSupernodeDP, node))
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
        uint midSupernodeRegularID = getID(supernodes, midSupernode);
        DataPoint midSupernodeDP = makeDataPointFromRegularID(midSupernodeRegularID);
        if (gt(midSupernodeDP, node))
          highSupernode = midSupernode;
        // == can't happen since node is regular
        else
          lowSupernode = midSupernode;
      } // binary search
      // now we can use the high node as the superparent
      superparent = highSupernode;
    }
  }
  return superparent;
}

void drawRaster(vec2 uv) {
  int x = int(uv.x*float(dims.x-1));
  int y = int(uv.y*float(dims.y-1));
  float xf = float(uv.x*float(dims.x-1));
  float yf = float(uv.y*float(dims.y-1));
  float xfrac = xf-float(x);
  float yfrac = yf-float(y);

  float eps = 2e-2f;
  if (xfrac < eps || 1.f-xfrac < eps || yfrac < eps || 1.f-yfrac < eps) {
    gl_FragData[0] = vec4(1,0,0,1);
  }

  if (abs(xfrac-yfrac) < eps) {
    gl_FragData[0] = vec4(1,0,0,1);
  }
}

void highlightSupernodes(vec2 uv) {
  int x = int(uv.x*float(dims.x-1));
  int y = int(uv.y*float(dims.y-1));
  float xf = float(uv.x*float(dims.x-1));
  float yf = float(uv.y*float(dims.y-1));
  float xfrac = xf-float(x);
  float yfrac = yf-float(y);

  float eps = 2e-1f;

  int m00 = x+y*dims.x;
  int m10 = (x+1)+y*dims.x;
  int m11 = (x+1)+(y+1)*dims.x;
  int m01 = x+(y+1)*dims.x;

  uint node = NO_SUCH_ELEMENT;
  if (xfrac < eps && yfrac < eps) {
    node = getID(sortIndices, uint(m00));
  }

  if (1.f-xfrac < eps && yfrac < eps) {
    node = getID(sortIndices, uint(m10));
  }

  if (1.f-xfrac < eps && 1.f-yfrac < eps) {
    node = getID(sortIndices, uint(m11));
  }

  if (xfrac < eps && 1.f-yfrac < eps) {
    node = getID(sortIndices, uint(m01));
  }

  if (!noSuchElement(node)) {
    uint superparent = getID(superparents, maskedIndex(node));
    if (getID(supernodes, maskedIndex(superparent)) == node)
      gl_FragData[0] = vec4(1,1,0,1);
  }
}

SHADER_MAIN()
{
  vec4 texColor = texture(actortexture, tcoordVCVSOutput);
  gl_FragData[0] = texColor;

  vec2 uv = tcoordVCVSOutput.xy;

  uint selected = NO_SUCH_ELEMENT;
  if (uvSelected.x>0 && uvSelected.y>0) {
    selected = locateSuperarc(uvSelected);
  }
  uint superparent = locateSuperarc(uv);

  if (superparent == selected)
    gl_FragData[0] = vec4(1,0.5,0,1);
  else
    gl_FragData[0] = lut(superparent);

  if (raster!=0)
    drawRaster(uv);

  highlightSupernodes(uv);

#ifdef DBG
  std::cout << "superparent: " << superparent << '\n';
#endif
}




