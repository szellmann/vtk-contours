// std
#include <stdio.h>
#include <fstream>
#include <iostream>
// vtk
#include <vtkCamera.h>
#include <vtkColorTransferFunction.h>
#include <vtkCompositeDataGeometryFilter.h>
#include <vtkContourFilter.h>
#include <vtkDataArraySelection.h>
#include <vtkFloatArray.h>
#include <vtkInformationDoubleVectorKey.h>
#include <vtkLogger.h>
#include <vtkNamedColors.h>
#include <vtkNew.h>
#include <vtkPointData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkStructuredPointsReader.h>
#include <vtkStructuredPoints.h>
#include <vtkUniformGrid.h>
#include <vtkLookupTable.h>
#include <vtkImageMapToColors.h>
#include <vtkImageActor.h>
#include <vtkImageMapper3D.h>
#include <vtkInteractorStyleImage.h>
#include <vtkShaderProperty.h>
#include <vtkPlaneSource.h>
#include <vtkTexture.h>
#include <vtkOpenGLRenderWindow.h>
#include <vtkOpenGLTexture.h>
#include <vtkTextureObject.h>
#include <vtkOpenGLTexture.h>
#include <vtkCallbackCommand.h>
#include <vtkCellPicker.h>
#include <vtkUniforms.h>
#include <vtkRenderStepsPass.h>
#include <vtkFramebufferPass.h>
#include <vtkPixelBufferObject.h>
// vktmlib
#include <vtkmlib/ArrayConverters.h>
#include <vtkmlib/ImageDataConverter.h>
// viskores
#include <viskores/filter/scalar_topology/ContourTreeUniformAugmented.h>
#include <viskores/cont/Initialize.h>
// ours
#include "ShaderDebugger.h"

#define GL_RGBA_INTEGER 0x8D99 // TODO!
#define GL_UNSIGNED_INT 0x1405 // TODO!!
//#define GL_NEAREST      0x2600 // TODO!!!
#define GL_RGBA32UI     0x8D70 // TODO!!!!
#define GL_RGBA         0x1908
#define GL_RGBA32F      0x8814
#define GL_FLOAT        0x1406

void replaceToken(std::string &source, const std::string &from, const std::string &to) {
  size_t pos = 0;
  while ((pos = source.find(from, pos)) != std::string::npos) {
    source.replace(pos, from.length(), to);
    pos += to.length();
  }
}

template<typename T>
inline std::vector<T> toStdVector(const viskores::cont::ArrayHandle<T> &array) {
  auto basicArray = static_cast<viskores::cont::ArrayHandleBasic<T>>(array);
  const T *ptr = basicArray.GetReadPointer();
  size_t length = array.GetNumberOfValues();
  std::vector<T> vec(length);
  std::memcpy(vec.data(),ptr,length*sizeof(T));
  return vec;
}

inline std::vector<uint32_t> toUI32(const std::vector<int64_t> &src) {
  std::vector<uint32_t> dst(src.size());
  for (size_t i=0; i<src.size(); ++i) {
    // Convert from 64-bit to 32-bit, 5 highest bit are reserved for flags:
    uint64_t flags64 = src[i] & 0xF800000000000000ull;
    uint32_t flags32 = uint32_t(flags64 >> 32ull);

    uint64_t maskedIndex64 = src[i] & 0x07FFFFFFFFFFFFFFull;
    uint32_t maskedIndex32 = uint32_t(maskedIndex64);

    if (uint32_t(maskedIndex64) != maskedIndex32) {
      std::cerr << "WARNING: no support for 64-bit indices!" << std::endl;
    }

    dst[i] = maskedIndex32 | flags32;
  }
  return dst;
}

inline void readLUT(std::string fileName, std::vector<float> &rgbaLUT) {
  std::ifstream in(fileName);
  if (!in.good()) return;

  std::string line;
  while (std::getline(in, line)) {
    int r, g, b, a;
    int res = sscanf(line.c_str(), "%i %i %i %i", &r, &g, &b, &a);
    if (res < 4) return;
    rgbaLUT.push_back(r/255.f);
    rgbaLUT.push_back(g/255.f);
    rgbaLUT.push_back(b/255.f);
    rgbaLUT.push_back(a/255.f);
  }
}

inline void dumpLUT(const std::vector<float> &rgbaLUT) {
  auto saturate = [](int x) {
    int lo=0;
    int hi=255;
    return std::min(std::max(x,lo),hi);
  };

  for (int i=0; i<rgbaLUT.size(); i+=4) {
    int r = saturate(int(rgbaLUT[i]*255));
    int g = saturate(int(rgbaLUT[i+1]*255));
    int b = saturate(int(rgbaLUT[i+2]*255));
    int a = 255;
    std::cout << r << ' ' << g << ' ' << b << ' ' << a << '\n';
  }
}

inline vtkSmartPointer<vtkOpenGLTexture> toTextureRGBA32UI(
    const std::vector<unsigned> &vec, vtkOpenGLRenderWindow *glWin)
{
  // why use texture objects: vtkTexture maps the values to [0:255] and
  // that can't be turned off...
  vtkNew<vtkTextureObject> to;

  to->SetContext(glWin);
  to->SetMinificationFilter(vtkTextureObject::Nearest);
  to->SetMagnificationFilter(vtkTextureObject::Nearest);

  auto divUp = [](int a, int b) { return (a+b-1)/b; };
  int width = std::min((int)vec.size(),4096); // TODO: find out platform texture dimensions
  int height = divUp(vec.size(),width);

  std::vector<unsigned> ui32(width*size_t(height)*4);
  std::memcpy(ui32.data(), vec.data(), sizeof(vec[0])*vec.size());

  to->SetFormat(GL_RGBA_INTEGER);
  to->SetDataType(GL_UNSIGNED_INT);
  to->SetInternalFormat(GL_RGBA32UI);
  bool uploaded = to->Create2DFromRaw(width, height, 4, VTK_UNSIGNED_INT, ui32.data());

  vtkSmartPointer<vtkOpenGLTexture> gl = vtkSmartPointer<vtkOpenGLTexture>::New();
  gl->SetTextureObject(to);

  return gl;
}

inline vtkSmartPointer<vtkOpenGLTexture> toTextureR32F(
    const std::vector<float> &vec, vtkOpenGLRenderWindow *glWin)
{
  // why use texture objects: vtkTexture maps the values to [0:255] and
  // that can't be turned off...
  vtkNew<vtkTextureObject> to;

  to->SetContext(glWin);
  to->SetMinificationFilter(vtkTextureObject::Nearest);
  to->SetMagnificationFilter(vtkTextureObject::Nearest);

  auto divUp = [](int a, int b) { return (a+b-1)/b; };
  int width = std::min((int)vec.size(),4096); // TODO: find out platform texture dimensions
  int height = divUp(vec.size(),width);

  bool uploaded = to->Create2DFromRaw(width, height, 1, VTK_FLOAT, (float *)vec.data());

  vtkSmartPointer<vtkOpenGLTexture> gl = vtkSmartPointer<vtkOpenGLTexture>::New();
  gl->SetTextureObject(to);

  return gl;
}

inline vtkSmartPointer<vtkOpenGLTexture> toTextureRGBA32F(
    const std::vector<float> &vec, vtkOpenGLRenderWindow *glWin)
{
  vtkNew<vtkTextureObject> to;

  to->SetContext(glWin);
  to->SetMinificationFilter(vtkTextureObject::Nearest);
  to->SetMagnificationFilter(vtkTextureObject::Nearest);

  auto divUp = [](int a, int b) { return (a+b-1)/b; };
  int width = 4096; // TODO: find out platform texture dimensions
  int height = divUp(vec.size(),width);

  bool uploaded = to->Create2DFromRaw(vec.size()/4, 1, 4, VTK_FLOAT, (float *)vec.data());

  vtkSmartPointer<vtkOpenGLTexture> gl = vtkSmartPointer<vtkOpenGLTexture>::New();
  gl->SetTextureObject(to);

  return gl;
}

struct AppState {
  vtkActor *actor{nullptr};
  vtkRenderer *renderer{nullptr};
  vtkImageData *imgData{nullptr};
  vtkShaderProperty *shaderProperty{nullptr};
  ShaderDebugger *shaderDebugger{nullptr};
  std::vector<int64_t> sortOrder, sortIndices, nodes, arcs, superparents, superarcs,
      supernodes, hyperparents, whenTransferred, hypernodes, hyperarcs;
} g_appState;


int main(int argc, char **argv)
{
  viskores::cont::Initialize(argc, argv, viskores::cont::InitializeOptions::AddHelp);

  std::string fileName(argv[1]);
  auto reader = vtkSmartPointer<vtkStructuredPointsReader>::New();
  reader->SetFileName(fileName.c_str());
  reader->Update();

  vtkImageData *imgData = reader->GetOutput();
  vtkDataArray* scalars = imgData->GetPointData()->GetScalars();
  g_appState.imgData = imgData;

  int dims[3];
  double spacing[3];
  double origin[3];
  double range[2];
  imgData->GetDimensions(dims);
  imgData->GetSpacing(spacing);
  imgData->GetOrigin(origin);
  scalars->GetRange(range);

  std::cout << "Input dims " << dims[0] << ',' << dims[1] << ',' << dims[2] << '\n';
  std::cout << "Input spacing " << spacing[0] << ',' << spacing[1] << ',' << spacing[2] << '\n';
  std::cout << "Input origin " << origin[0] << ',' << origin[1] << ',' << origin[2] << '\n';
  std::cout << "Input range " << range[0] << ',' << range[1] << '\n';

  // linear data array
  std::vector<float> data;
  for (vtkIdType i=0; i<imgData->GetNumberOfPoints(); ++i) {
    data.push_back((float)scalars->GetTuple1(i));
  }

  auto lut = vtkSmartPointer<vtkLookupTable>::New();
  lut->SetTableRange(range[0], range[1]);
  lut->Build();

  auto colorMapped = vtkSmartPointer<vtkImageMapToColors>::New();
  colorMapped->SetInputData(imgData);
  colorMapped->SetLookupTable(lut);
  colorMapped->SetOutputFormatToRGBA();
  colorMapped->Update();

  // serial contour tree to pass into FS:
  std::vector<int64_t> &sortOrder = g_appState.sortOrder,
      &sortIndices = g_appState.sortIndices,
      &nodes = g_appState.nodes, &arcs = g_appState.arcs,
      &superparents  = g_appState.superparents, &superarcs = g_appState.superarcs,
      &supernodes = g_appState.supernodes, &hyperparents = g_appState.hyperparents,
      &whenTransferred = g_appState.whenTransferred, &hypernodes = g_appState.hypernodes,
      &hyperarcs = g_appState.hyperarcs;

  try {
    viskores::cont::DataSet viskoresData = tovtkm::Convert(imgData);
    // manually add scalar field to viskores data set as the ImageData
    // converter doesn't pick this up:
    std::string scalarFieldName = "MyScalars"; // that's the name assigned by our converter.. (TODO)
    if (!viskoresData.HasField(scalarFieldName)) {
      auto viskoresScalars = tovtkm::Convert(scalars,0);
      viskoresData.AddField(viskores::cont::Field(
        viskoresScalars.GetName(),
        viskores::cont::Field::Association::Points,
        viskoresScalars.GetData()
      ));
    }
    viskores::filter::scalar_topology::ContourTreeAugmented filter;
    filter.SetActiveField(scalarFieldName);

    filter.Execute(viskoresData);

    std::cout << "[Viskores] Computing contour tree...\n";
    auto contourTree = filter.GetContourTree();

    //contourTree.PrintContent();
    //std::cout << contourTree.PrintArraySizes() << '\n';

    // sort order: from regular to mesh IDs
    sortOrder = toStdVector(filter.GetSortOrder());
    // sort indices: inverse mapping
    sortIndices.resize(sortOrder.size());
    for (int i=0; i<sortIndices.size(); ++i) {
      sortIndices[sortOrder[i]] = i;
    }
    nodes = toStdVector(contourTree.Nodes);
    arcs = toStdVector(contourTree.Arcs);
    superparents = toStdVector(contourTree.Superparents);
    superarcs = toStdVector(contourTree.Superarcs);
    supernodes = toStdVector(contourTree.Supernodes);
    hyperparents = toStdVector(contourTree.Hyperparents);
    whenTransferred = toStdVector(contourTree.WhenTransferred);
    hypernodes = toStdVector(contourTree.Hypernodes);
    hyperarcs = toStdVector(contourTree.Hyperarcs);
  } catch (const viskores::cont::Error& error) {
    std::cerr << "[Viskores]: " << error.GetMessage() << std::endl;
    return EXIT_FAILURE;
  }

  double aspect=dims[0]/double(dims[1]);
  auto planeSource = vtkSmartPointer<vtkPlaneSource>::New();
  planeSource->SetOrigin(0.0, 0.0, 0.0);
  planeSource->SetPoint1(1.0*aspect, 0.0, 0.0);
  planeSource->SetPoint2(0.0, 1.0, 0.0);

  auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
  mapper->SetInputConnection(planeSource->GetOutputPort());

  auto texture = vtkSmartPointer<vtkTexture>::New();
  texture->SetInputConnection(colorMapped->GetOutputPort());
  texture->InterpolateOn(); 

  auto actor = vtkSmartPointer<vtkActor>::New();
  g_appState.actor = actor;
  actor->PickableOn();
  actor->SetMapper(mapper);
  actor->SetTexture(texture);

  auto renderer = vtkSmartPointer<vtkRenderer>::New();
  g_appState.renderer = renderer;

  auto renderWindow = vtkSmartPointer<vtkRenderWindow>::New();
  renderWindow->SetSize(1024,1024);


  // render once to enforce context creation:
  renderWindow->Render();
  renderWindow->MakeCurrent();
  auto* glWin = vtkOpenGLRenderWindow::SafeDownCast(renderWindow);

  float rangef[2] = { (float)range[0], (float)range[1] };

  vtkShaderProperty* shaderProperty = actor->GetShaderProperty();
  g_appState.shaderProperty = shaderProperty;
  vtkUniforms *uniforms = shaderProperty->GetFragmentCustomUniforms();

  std::vector<float> rgbaLUT;
  if (argc > 2) {
    std::string lutFileName(argv[2]);
    readLUT(lutFileName,rgbaLUT);
  }

  if (rgbaLUT.size() < supernodes.size()*4) {
    rgbaLUT.resize(supernodes.size()*4);
    for (int i=0; i<rgbaLUT.size(); i+=4) {
      rgbaLUT[i] = rgbaLUT[i+1] = rgbaLUT[i+2] = i/float(rgbaLUT.size()-4);
      rgbaLUT[i+3] = 1.f;
    }
  }
  //dumpLUT(rgbaLUT);

  std::string dbg = std::string(PLUGIN_PATH)+std::string("/libShaderDebugger.dylib");

  ShaderDebugger shaderDebugger(dbg);
  if (!shaderDebugger.good()) return EXIT_FAILURE;
  g_appState.shaderDebugger = &shaderDebugger;

  // upload data set
  uniforms->SetUniform2i("dims", dims);
  uniforms->SetUniform2f("range", rangef);
  actor->GetProperty()->SetTexture("data", toTextureR32F(data,glWin));
  // upload contour tree
  actor->GetProperty()->SetTexture("sortOrder", toTextureRGBA32UI(toUI32(sortOrder),glWin));
  actor->GetProperty()->SetTexture("sortIndices", toTextureRGBA32UI(toUI32(sortIndices),glWin));
  actor->GetProperty()->SetTexture("nodes", toTextureRGBA32UI(toUI32(nodes),glWin));
  actor->GetProperty()->SetTexture("arcs", toTextureRGBA32UI(toUI32(arcs),glWin));
  actor->GetProperty()->SetTexture("superparents", toTextureRGBA32UI(toUI32(superparents),glWin));
  actor->GetProperty()->SetTexture("superarcs", toTextureRGBA32UI(toUI32(superarcs),glWin));
  actor->GetProperty()->SetTexture("supernodes", toTextureRGBA32UI(toUI32(supernodes),glWin));
  actor->GetProperty()->SetTexture("hyperparents", toTextureRGBA32UI(toUI32(hyperparents),glWin));
  actor->GetProperty()->SetTexture("whenTransferred", toTextureRGBA32UI(toUI32(whenTransferred),glWin));
  actor->GetProperty()->SetTexture("hypernodes", toTextureRGBA32UI(toUI32(hypernodes),glWin));
  actor->GetProperty()->SetTexture("hyperarcs", toTextureRGBA32UI(toUI32(hyperarcs),glWin));
  uniforms->SetUniformi("numHypernodes", (int)hypernodes.size());
  uniforms->SetUniformi("numSupernodes", (int)supernodes.size());
  // TF
  actor->GetProperty()->SetTexture("rgbaLUT", toTextureRGBA32F(rgbaLUT,glWin));
  // interaction
  float uvSelected[2] = {-1.f,-1.f};
  uniforms->SetUniform2f("uvSelected", uvSelected);

  /* shader debugger */

  // upload data set
  shaderDebugger.setUniform2i("dims", dims);
  shaderDebugger.setUniform2f("range", rangef);
  shaderDebugger.setTextureR32F("data", data);
  // upload countour tree
  shaderDebugger.setTextureRGBA32UI("sortOrder", toUI32(sortOrder));
  shaderDebugger.setTextureRGBA32UI("sortIndices", toUI32(sortIndices));
  shaderDebugger.setTextureRGBA32UI("nodes", toUI32(nodes));
  shaderDebugger.setTextureRGBA32UI("arcs", toUI32(arcs));
  shaderDebugger.setTextureRGBA32UI("superparents", toUI32(superparents));
  shaderDebugger.setTextureRGBA32UI("superarcs", toUI32(superarcs));
  shaderDebugger.setTextureRGBA32UI("supernodes", toUI32(supernodes));
  shaderDebugger.setTextureRGBA32UI("hyperparents", toUI32(hyperparents));
  shaderDebugger.setTextureRGBA32UI("whenTransferred", toUI32(whenTransferred));
  shaderDebugger.setTextureRGBA32UI("hypernodes", toUI32(hypernodes));
  shaderDebugger.setTextureRGBA32UI("hyperarcs", toUI32(hyperarcs));
  shaderDebugger.setUniformi("numHypernodes", (int)hypernodes.size());
  shaderDebugger.setUniformi("numSupernodes", (int)supernodes.size());
  // TF
  shaderDebugger.setTextureRGBA32F("rgbaLUT", rgbaLUT);

  shaderDebugger.fragment(0,0);

  std::ifstream shaderFile(std::string(SHADER_PATH)+"/segmentation.glsl");
  std::string shaderSource((std::istreambuf_iterator<char>(shaderFile)),
                            std::istreambuf_iterator<char>());
  replaceToken(shaderSource, "SHADER_MAIN", "void main");
  shaderProperty->SetFragmentShaderCode(shaderSource.c_str());

  renderWindow->AddRenderer(renderer);
  renderWindow->SetWindowName("Viewer");

  auto interactor = vtkSmartPointer<vtkRenderWindowInteractor>::New();
  interactor->SetRenderWindow(renderWindow);

  auto style = vtkSmartPointer<vtkInteractorStyleImage>::New();
  interactor->SetInteractorStyle(style);

  vtkNew<vtkCallbackCommand> onClick;
  onClick->SetCallback(
    [](vtkObject *caller, long unsigned int eventID, void *clientData, void *callData) {
      auto *iren = dynamic_cast<vtkRenderWindowInteractor *>(caller);
      if (!iren) return;
      auto *appState = (AppState *)clientData;
      if (!appState) return;

      int *pos = iren->GetEventPosition();

      vtkNew<vtkCellPicker> cellPicker;

      // for now use the picking code primarily for debugging
      if (cellPicker->Pick(pos[0], pos[1], 0, appState->renderer)) {
        vtkActor *hitActor = cellPicker->GetActor();
        if (hitActor) {
          vtkIdType cellId = cellPicker->GetCellId();
          vtkIdType pointId = cellPicker->GetPointId();

          double *uvw = cellPicker->GetPCoords();
          float uvSelected[2] = {(float)uvw[0],(float)uvw[1]};
          vtkUniforms *uniforms = appState->shaderProperty->GetFragmentCustomUniforms();
          uniforms->SetUniform2f("uvSelected", uvSelected);

          appState->shaderDebugger->fragment(uvSelected[0],uvSelected[1]);
          appState->shaderDebugger->printFragData(0);
        }
      } else  {
        vtkUniforms *uniforms = appState->shaderProperty->GetFragmentCustomUniforms();
        float uvSelected[2] = {-1.f,-1.f};
        uniforms->SetUniform2f("uvSelected", uvSelected);
      }
    });
  onClick->SetClientData(&g_appState);
  interactor->AddObserver(vtkCommand::LeftButtonPressEvent , onClick);

  // key 's' hot-reloads the shader:
  vtkNew<vtkCallbackCommand> onKey;
  onKey->SetCallback(
    [](vtkObject *caller, long unsigned int eventID, void *clientData, void *callData) {
      auto *interactor = vtkRenderWindowInteractor::SafeDownCast(caller);
      if (!interactor) return;
      std::string key = interactor->GetKeySym();
      if (key == "s") {
        std::ifstream shaderFile(std::string(SHADER_PATH)+"/segmentation.glsl");
        std::string shaderSource((std::istreambuf_iterator<char>(shaderFile)),
            std::istreambuf_iterator<char>());

        auto *appState = (AppState *)clientData;
        if (!appState) return;

        std::cout << "Reloading shader source!\n";
        appState->shaderProperty->SetFragmentShaderCode(shaderSource.c_str());
      }
    });
  onKey->SetClientData(&g_appState);
  interactor->AddObserver(vtkCommand::KeyPressEvent, onKey);

  renderer->AddActor(actor);
  vtkNew<vtkNamedColors> colors;
  renderer->SetBackground(colors->GetColor3d("CornflowerBlue").GetData());
  renderer->ResetCamera();

  renderWindow->Render();

  interactor->Start();

  return EXIT_SUCCESS;
}



