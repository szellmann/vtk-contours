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
uniform usampler2D hyperarcs;

// ./filter/scalar_topology/worklet/contourtree_augmented//Types.h
#define NO_SUCH_ELEMENT 0x8000000u
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

// 64-bit ID type
struct ID_t {
  uint lo, hi;
};

// 32-bit index from 64-bit ID
uint maskedIndex(ID_t id) {
  return id.lo; // TODO..
}

bool noSuchElement(ID_t id) {
  return (id.hi & NO_SUCH_ELEMENT) != 0u;
}

struct ui64x2_t {
  uint lo[2], hi[2];
};

ID_t getID(usampler2D samp, int index) {
  int i=index%4096;
  int j=index/4096;
  uvec4 ui4 = texelFetch(samp, ivec2(i,j), 0);
  if (index%2==0)
    return ID_t(ui4.x,ui4.z);
  else
    return ID_t(ui4.y,ui4.w);
}

ID_t getID(usampler2D samp, uint index) {
  return getID(samp, int(index));
}

ID_t getID(usampler2D samp, ID_t id) {
  return getID(samp, int(id.lo));
}

float getData(int nodeID) {
  int i=nodeID%4096;
  int j=nodeID/4096;
  vec4 f4 = texelFetch(data, ivec2(i,j), 0);
  return f4.r;
}

float bary(float a, float b, float c, float u, float v) {
  float s2 = c*v;
  float s3 = b*u;
  float s1 = a*(1.0f-(u+v));
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

  int tri0[3]; tri0[0]=p0; tri0[1]=p1; tri0[2]=p2;
  int tri1[3]; tri1[0]=p0; tri1[1]=p2; tri1[2]=p3;

  float dat0[3]; dat0[0]=getData(p0); dat0[1]=getData(p1); dat0[2]=getData(p2);
  float dat1[3]; dat1[0]=getData(p0); dat1[1]=getData(p2); dat1[2]=getData(p3);
  int bottom=-1, top=-1;
  float minValue=1e31f, maxValue=-1e31f;

  // in the bottom/right triangle, u-coord "grows" faster,
  // in the top/left triangle, v-coord "grows" faster:
  int triID = (xfrac >= yfrac) ? 0 : 1;

  // function value of node on triangle surface
  float value = (triID==0) ? bary(dat0[0],dat0[1],dat0[2],xfrac-yfrac,yfrac)
                           : bary(dat1[0],dat1[1],dat1[2],xfrac,yfrac-xfrac);

  // compute top and bottom ID (triangle vertices with max and min value):
  for (int i=0; i<3; ++i) {
    if (triID==0) {
      if (dat0[i] < minValue) {
        minValue = dat0[i];
        bottom = tri0[i];
      }

      if (dat0[2-i] > maxValue) {
        maxValue = dat0[2-i];
        top = tri0[2-i];
      }
    } else {
      if (dat1[i] < minValue) {
        minValue = dat1[i];
        bottom = tri1[i];
      }

      if (dat1[2-i] > maxValue) {
        maxValue = dat1[2-i];
        top = tri1[2-i];
      }
    }
  }

  // BEGIN LocateSuperarcs
  if (true) {
    // regular nodes only
    // we will need to prune top and bottom until one of them prunes past the node
    ID_t topSuperparent = getID(superparents, top);
    ID_t bottomSuperparent = getID(superparents, bottom);
    // and we can also find out when they transferred
    ID_t topWhen = getID(whenTransferred, topSuperparent);
    ID_t bottomWhen = getID(whenTransferred, bottomSuperparent);
    // and their hyperparent
    ID_t topHyperparent = getID(hyperparents, topSuperparent);
    ID_t bottomHyperparent = getID(hyperparents, bottomSuperparent);

    gl_FragData[0] = vec4(randomColor(maskedIndex(bottomHyperparent)), 1.f);
  }

  float f = (value-range.x)/(range.y-range.x);
  gl_FragData[0] = vec4(vec3(f),1.f);
  //gl_FragData[0] = texColor;
}




