//VTK::System::Dec
//VTK::Output::Dec
in vec2 tcoordVCVSOutput;
uniform sampler2D actortexture;
// data set
uniform ivec2 dims;
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
uint getIndex(ID_t id) {
  return id.lo;
}

// upper 5 bits contain flags
uint getFlags(ID_t id) {
  return (id.hi >> 27u) & 0x1Fu;
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

float getData(int nodeID) {
  int i=nodeID%4096;
  int j=nodeID/4096;
  vec4 f4 = texelFetch(data, ivec2(i,j), 0);
  return f4.r;
}

void main() {
  vec4 texColor = texture(actortexture, tcoordVCVSOutput);
  gl_FragData[0] = texColor;

  vec2 uv = tcoordVCVSOutput.xy;
  int x = int(uv.x*float(dims.x-1));
  int y = int(uv.y*float(dims.y-1));
  float xf = float(uv.x*float(dims.x-1));
  float yf = float(uv.y*float(dims.y-1));
  float xfrac = xf-float(x);
  float yfrac = yf-float(y);

  int p0 = x+y*dims.x;
  int p1 = (x+1)+y*dims.x;
  int p2 = x+(y+1)*dims.x;
  int p3 = (x+1)+(y+1)*dims.x;

  int tri0[3]; tri0[0]=p0; tri0[1]=p1; tri0[2]=p2;
  int tri1[3]; tri1[0]=p0; tri1[1]=p2; tri1[2]=p3;

  float dat0[3]; dat0[0]=getData(p0); dat0[1]=getData(p1); dat0[2]=getData(p2);
  float dat1[3]; dat1[0]=getData(p0); dat1[1]=getData(p2); dat1[2]=getData(p3);
  int vlo=-1, vhi=-1;
  float minValue=1e31f, maxValue=-1e31f;
  int triID = (xfrac >= yfrac) ? 0 : 1;

  for (int i=0; i<3; ++i) {
    if (triID==0) {
      if (dat0[i] < minValue) {
        minValue = dat0[i];
        vlo = tri0[i];
      }

      if (dat0[2-i] > maxValue) {
        maxValue = dat0[2-i];
        vhi = tri0[2-i];
      }
    } else {
      if (dat1[i] < minValue) {
        minValue = dat1[i];
        vlo = tri1[i];
      }

      if (dat1[2-i] > maxValue) {
        maxValue = dat1[2-i];
        vhi = tri1[2-i];
      }
    }
  }

  uint SP = getIndex(getID(superparents, vhi));
  gl_FragData[0] = vec4(randomColor(SP), 1.f);
}




