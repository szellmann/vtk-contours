// std
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

#define GL_RGBA_INTEGER 0x8D99 // TODO!
#define GL_UNSIGNED_INT 0x1405 // TODO!!
//#define GL_NEAREST      0x2600 // TODO!!!
#define GL_RGBA32UI     0x8D70 // TODO!!!!
#define GL_RGBA         0x1908
#define GL_RGBA32F      0x8814
#define GL_FLOAT        0x1406

template<typename T>
inline std::vector<T> toStdVector(const viskores::cont::ArrayHandle<T> &array) {
  auto basicArray = static_cast<viskores::cont::ArrayHandleBasic<T>>(array);
  const T *ptr = basicArray.GetReadPointer();
  size_t length = array.GetNumberOfValues();
  std::vector<T> vec(length);
  std::memcpy(vec.data(),ptr,length*sizeof(T));
  return vec;
}

inline void toUI32(std::vector<uint32_t> &dst, const std::vector<int64_t> &src) {
  if (dst.size() < src.size())
    dst.resize(src.size());

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
}

void writePPM(std::string fileName,
              const std::vector<uint8_t> &pixels,
              unsigned width,
              unsigned height,
              unsigned components)
{
  std::ofstream of(fileName);
  if (!of) return;

  of << "P3\n" << width << " " << height << "\n255\n";
  for (unsigned y=height-1; y>=0; --y) {
    for (unsigned x=0; x<width; ++x) {
      unsigned pixelID = (y * width + x) * components;
      int r = pixels[pixelID];
      int g = pixels[pixelID+1];
      int b = pixels[pixelID+2];

      of << r << ' ' << g << ' ' << b << "    ";
    }
    of << '\n';
  }
}

inline vtkSmartPointer<vtkOpenGLTexture> toTextureUI(
    const std::vector<int64_t> &vec, vtkOpenGLRenderWindow *glWin)
{
  // why use texture objects: vtkTexture maps the values to [0:255] and
  // that can't be turned off...
  vtkNew<vtkTextureObject> to;

  to->SetContext(glWin);
  to->SetMinificationFilter(vtkTextureObject::Nearest);
  to->SetMagnificationFilter(vtkTextureObject::Nearest);

  auto divUp = [](int a, int b) { return (a+b-1)/b; };
  int width = 4096; // TODO: find out platform texture dimensions
  int height = divUp(vec.size(),width);

  std::vector<unsigned> ui32(width*size_t(height)*4);
  toUI32(ui32,vec);

  to->SetFormat(GL_RGBA_INTEGER);
  to->SetDataType(GL_UNSIGNED_INT);
  to->SetInternalFormat(GL_RGBA32UI);
  bool uploaded = to->Create2DFromRaw(width, height, 4, VTK_UNSIGNED_INT, ui32.data());

  vtkSmartPointer<vtkOpenGLTexture> gl = vtkSmartPointer<vtkOpenGLTexture>::New();
  gl->SetTextureObject(to);

  return gl;
}

inline vtkSmartPointer<vtkOpenGLTexture> toTextureF(
    const std::vector<float> &vec, vtkOpenGLRenderWindow *glWin)
{
  // why use texture objects: vtkTexture maps the values to [0:255] and
  // that can't be turned off...
  vtkNew<vtkTextureObject> to;

  to->SetContext(glWin);
  to->SetMinificationFilter(vtkTextureObject::Nearest);
  to->SetMagnificationFilter(vtkTextureObject::Nearest);

  auto divUp = [](int a, int b) { return (a+b-1)/b; };
  int width = 4096; // TODO: find out platform texture dimensions
  int height = divUp(vec.size(),width);

  bool uploaded = to->Create2DFromRaw(width, height, 1, VTK_FLOAT, (float *)vec.data());

  vtkSmartPointer<vtkOpenGLTexture> gl = vtkSmartPointer<vtkOpenGLTexture>::New();
  gl->SetTextureObject(to);

  return gl;
}

struct AppState {
  vtkActor *actor{nullptr};
  vtkRenderer *renderer{nullptr};
  vtkImageData *imgData{nullptr};
  vtkShaderProperty *shaderProperty{nullptr};
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

  // upload data set
  uniforms->SetUniform2i("dims", dims);
  uniforms->SetUniform2f("range", rangef);
  actor->GetProperty()->SetTexture("data", toTextureF(data,glWin));
  // upload contour tree
  actor->GetProperty()->SetTexture("sortOrder", toTextureUI(sortOrder,glWin));
  actor->GetProperty()->SetTexture("sortIndices", toTextureUI(sortIndices,glWin));
  actor->GetProperty()->SetTexture("nodes", toTextureUI(nodes,glWin));
  actor->GetProperty()->SetTexture("arcs", toTextureUI(arcs,glWin));
  actor->GetProperty()->SetTexture("superparents", toTextureUI(superparents,glWin));
  actor->GetProperty()->SetTexture("superarcs", toTextureUI(superarcs,glWin));
  actor->GetProperty()->SetTexture("supernodes", toTextureUI(supernodes,glWin));
  actor->GetProperty()->SetTexture("hyperparents", toTextureUI(hyperparents,glWin));
  actor->GetProperty()->SetTexture("whenTransferred", toTextureUI(whenTransferred,glWin));
  actor->GetProperty()->SetTexture("hypernodes", toTextureUI(hypernodes,glWin));
  actor->GetProperty()->SetTexture("hyperarcs", toTextureUI(hyperarcs,glWin));
  uniforms->SetUniformi("numHypernodes", (int)hypernodes.size());
  uniforms->SetUniformi("numSupernodes", (int)supernodes.size());
  // interaction
  float uvSelected[2] = {-1.f,-1.f};
  uniforms->SetUniform2f("uvSelected", uvSelected);

  std::ifstream shaderFile(std::string(SHADER_PATH)+"/segmentation.glsl");
  std::string shaderSource((std::istreambuf_iterator<char>(shaderFile)),
                            std::istreambuf_iterator<char>());
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
        }
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

#if 0
  vtkNew<vtkRenderStepsPass> basicPasses;
  vtkNew<vtkFramebufferPass> fboPass;
  fboPass->SetDelegatePass(basicPasses);
  renderer->SetPass(fboPass);

  vtkNew<vtkPlaneSource> targetPlane;
  vtkNew<vtkPolyDataMapper> targetMapper;
  targetMapper->SetInputConnection(targetPlane->GetOutputPort());

  vtkNew<vtkOpenGLTexture> targetTexture;
  auto *texObj = fboPass->GetColorTexture();
  targetTexture->SetTextureObject(texObj);

  vtkNew<vtkActor> targetActor;
  targetActor->SetMapper(targetMapper);
  targetActor->SetTexture(targetTexture);
#endif

  renderWindow->Render();

#if 0
  if (texObj) {
    unsigned texDims[] = {
      texObj->GetWidth(),
      texObj->GetHeight(),
    };
    std::vector<uint8_t> pixels(texDims[0]*texDims[1]*texObj->GetComponents());

    auto *pbo = texObj->Download(texObj->GetTarget(), 0);
    vtkIdType strides[2] = {0,0};
    bool success = pbo && pbo->Download2D(VTK_UNSIGNED_CHAR,
                                          pixels.data(),
                                          texDims, 
                                          texObj->GetComponents(),
                                          strides);
    pbo->Delete();

    if (success) {
      writePPM(fileName+".ppm",pixels,texDims[0],texDims[1],texObj->GetComponents());
    }
  }
#endif

  interactor->Start();

  return EXIT_SUCCESS;
}



