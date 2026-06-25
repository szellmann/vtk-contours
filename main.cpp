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
#include <vtkTextureObject.h>
#include <vtkOpenGLTexture.h>
#include <vtkCallbackCommand.h>
#include <vtkCellPicker.h>
// vktmlib
#include <vtkmlib/ArrayConverters.h>
#include <vtkmlib/ImageDataConverter.h>
// viskores
#include <viskores/filter/scalar_topology/ContourTreeUniformAugmented.h>
#include <viskores/cont/Initialize.h>

template<typename T>
inline std::vector<T> toStdVector(const viskores::cont::ArrayHandle<T> &array) {
  auto basicArray = static_cast<viskores::cont::ArrayHandleBasic<T>>(array);
  const T *ptr = basicArray.GetReadPointer();
  size_t length = array.GetNumberOfValues();
  std::vector<T> vec(length);
  std::memcpy(vec.data(),ptr,length*sizeof(T));
  return vec;
}

inline vtkSmartPointer<vtkOpenGLTexture> toTexture(
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

  struct ui64x2_t {
    unsigned lo[2], hi[2];
  };
  std::vector<ui64x2_t> ui64x2(width*size_t(height)/2);
  for (size_t i=0; i<vec.size(); ++i) {
    ui64x2[i/2].lo[i%2] = ((unsigned)vec[i])&0xFFFFFFFFul;
    ui64x2[i/2].hi[i%2] = ((unsigned)(vec[i]>>32))&0xFFFFFFFFul;
  }

  #define GL_RGBA_INTEGER 0x8D99 // TODO!
  #define GL_UNSIGNED_INT 0x1405 // TODO!!
  #define GL_NEAREST      0x2600 // TODO!!!
  #define GL_RGBA32UI     0x8D70 // TODO!!!!
  to->SetFormat(GL_RGBA_INTEGER);
  to->SetDataType(GL_UNSIGNED_INT);
  to->SetInternalFormat(GL_RGBA32UI);
  to->SetMinificationFilter(GL_NEAREST);
  to->SetMagnificationFilter(GL_NEAREST);
  bool uploaded = to->Create2DFromRaw(width, height, 4, VTK_UNSIGNED_INT, ui64x2.data());

  vtkSmartPointer<vtkOpenGLTexture> gl = vtkSmartPointer<vtkOpenGLTexture>::New();
  gl->SetTextureObject(to);

  return gl;
}

struct AppState {
  vtkActor *actor{nullptr};
  vtkRenderer *renderer{nullptr};
  vtkImageData *imgData{nullptr};
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

  auto lut = vtkSmartPointer<vtkLookupTable>::New();
  lut->SetTableRange(range[0], range[1]);
  lut->Build();

  auto colorMapped = vtkSmartPointer<vtkImageMapToColors>::New();
  colorMapped->SetInputData(imgData);
  colorMapped->SetLookupTable(lut);
  colorMapped->SetOutputFormatToRGBA();
  colorMapped->Update();

  // serial contour tree to pass into FS:
  std::vector<int64_t> nodes, arcs, superparents, superarcs, supernodes, hyperparents,
      whenTransferred, hypernodes, hyperarcs;

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

  vtkShaderProperty* shaderProperty = actor->GetShaderProperty();

  actor->GetProperty()->SetTexture("nodes", toTexture(nodes,glWin));
  actor->GetProperty()->SetTexture("arcs", toTexture(arcs,glWin));
  actor->GetProperty()->SetTexture("superparents", toTexture(superparents,glWin));
  actor->GetProperty()->SetTexture("superarcs", toTexture(superarcs,glWin));
  actor->GetProperty()->SetTexture("supernodes", toTexture(supernodes,glWin));
  actor->GetProperty()->SetTexture("hyperparents", toTexture(hyperparents,glWin));
  actor->GetProperty()->SetTexture("whenTransferred", toTexture(whenTransferred,glWin));
  actor->GetProperty()->SetTexture("hyperarcs", toTexture(hyperarcs,glWin));

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

          // convert from parametric picking coordinates to image space:
          int dims[3];
          appState->imgData->GetDimensions(dims);
          double *uvw = cellPicker->GetPCoords();
          int x = uvw[0]*(dims[0]-1);
          int y = uvw[1]*(dims[1]-1);
          double xfrac = (uvw[0]*(dims[0]-1))-x;
          double yfrac = (uvw[1]*(dims[1]-1))-y;

          vtkIdType p0 = appState->imgData->FindPoint(x,y,0.0);
          vtkIdType p1 = appState->imgData->FindPoint(x+1,y,0.0);
          vtkIdType p2 = appState->imgData->FindPoint(x,y+1,0.0);
          vtkIdType p3 = appState->imgData->FindPoint(x+1,y+1,0.0);

          vtkDataArray* scalars = appState->imgData->GetPointData()->GetScalars();

          vtkIdType index[2][3] = {{ p0, p1, p2, }, { p0, p2, p3, }};

          double data[2][3] = {{
            scalars->GetTuple1(p0),
            scalars->GetTuple1(p1),
            scalars->GetTuple1(p2),
          }, {
            scalars->GetTuple1(p0),
            scalars->GetTuple1(p2),
            scalars->GetTuple1(p3),
          }};

          vtkIdType vlo=~0u, vhi=~0u;
          double minValue=INFINITY, maxValue=-INFINITY;
          int triID = (xfrac >= yfrac) ? 0 : 1;

          for (int i=0; i<3; ++i) {
            if (data[triID][i] < minValue) {
              minValue = data[triID][i];
              vlo = index[triID][i];
            }

            if (data[triID][2-i] > maxValue) {
              maxValue = data[triID][2-i];
              vhi = index[triID][2-i];
            }
          }

          #if 0
          std::cout << triID << '\n';
          std::cout << minValue << ',' << maxValue << '\n';
          std::cout << vlo << ',' << vhi << '\n';
          std::cout << p0 << ',' << p1 << ',' << p2 << ',' << p3 << '\n';
          std::cout << scalars->GetTuple1(p0) << '\n';
          std::cout << scalars->GetTuple1(p1) << '\n';
          std::cout << scalars->GetTuple1(p2) << '\n';
          std::cout << scalars->GetTuple1(p3) << '\n';
          std::cout << '\n';
          #endif
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

        auto *shaderProperty = (vtkShaderProperty *)clientData;
        if (!shaderProperty) return;

        std::cout << "Reloading shader source!\n";
        shaderProperty->SetFragmentShaderCode(shaderSource.c_str());
      }
    });
  onKey->SetClientData(shaderProperty);
  interactor->AddObserver(vtkCommand::KeyPressEvent, onKey);

  renderer->AddActor(actor);
  vtkNew<vtkNamedColors> colors;
  renderer->SetBackground(colors->GetColor3d("CornflowerBlue").GetData());
  renderer->ResetCamera();

  renderWindow->Render();
  interactor->Start();

  return EXIT_SUCCESS;
}



