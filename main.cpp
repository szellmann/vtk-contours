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
  // convert to float32: GL_R32I doesn't work on Mac. Robust up to 2^24...
  // use texture objects: vtkTexture maps the values to [0:255] and
  // that can't be turne off...
  vtkNew<vtkTextureObject> to;

  to->SetContext(glWin);
  to->SetMinificationFilter(vtkTextureObject::Nearest);
  to->SetMagnificationFilter(vtkTextureObject::Nearest);

  auto divUp = [](int a, int b) { return (a+b-1)/b; };
  int width = 4096; // TODO: find out platform texture dimensions
  int height = divUp(vec.size(),width);

  std::vector<float> v32f(width*size_t(height));
  for (size_t i=0; i<vec.size(); ++i) v32f[i] = vec[i];

  bool uploaded = to->Create2DFromRaw(width, height, 1, VTK_FLOAT, v32f.data());

  vtkSmartPointer<vtkOpenGLTexture> gl = vtkSmartPointer<vtkOpenGLTexture>::New();
  gl->SetTextureObject(to);

  return gl;
}

int main(int argc, char **argv)
{
  viskores::cont::Initialize(argc, argv, viskores::cont::InitializeOptions::AddHelp);

  std::string fileName(argv[1]);
  auto reader = vtkSmartPointer<vtkStructuredPointsReader>::New();
  reader->SetFileName(fileName.c_str());
  reader->Update();

  vtkImageData *imgData = reader->GetOutput();
  vtkDataArray* scalars = imgData->GetPointData()->GetScalars();

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
  actor->SetMapper(mapper);
  actor->SetTexture(texture);

  auto renderer = vtkSmartPointer<vtkRenderer>::New();
  auto renderWindow = vtkSmartPointer<vtkRenderWindow>::New();

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

  shaderProperty->SetFragmentShaderCode(R"(
//VTK::System::Dec
//VTK::Output::Dec
in vec2 tcoordVCVSOutput;
uniform sampler2D actortexture;
uniform sampler2D nodes;
uniform sampler2D arcs;
uniform sampler2D superparents;
uniform sampler2D superarcs;
uniform sampler2D supernodes;
uniform sampler2D hyperparents;
uniform sampler2D whenTransferred;
uniform sampler2D hyperarcs;

vec4 access(sampler2D samp, int index) {
  int i=index%4096;
  int j=index/4096;
  return texelFetch(samp, ivec2(i,j), 0);
}

void main() {
  vec4 texColor = texture(actortexture, tcoordVCVSOutput);

  // some tests with known data

#if 1
  vec4 n = access(nodes, 4097);
  if (n.x == 188526)
    gl_FragData[0] = vec4(1.f-texColor.xyz, 1.f);
  else
    gl_FragData[0] = vec4(0,1,0,1);

  return;
#endif

#if 0
  vec4 sa = access(superarcs, 300);
  if (sa.x == 301)
    gl_FragData[0] = vec4(1.f-texColor.xyz, 1.f);
  else
    gl_FragData[0] = vec4(0,1,0,1);

  return;
#endif

  gl_FragData[0] = texColor;
}
      )");

  renderWindow->AddRenderer(renderer);
  renderWindow->SetWindowName("Viewer");

  auto interactor = vtkSmartPointer<vtkRenderWindowInteractor>::New();
  interactor->SetRenderWindow(renderWindow);

  auto style = vtkSmartPointer<vtkInteractorStyleImage>::New();
  interactor->SetInteractorStyle(style);

  renderer->AddActor(actor);
  vtkNew<vtkNamedColors> colors;
  renderer->SetBackground(colors->GetColor3d("CornflowerBlue").GetData());
  renderer->ResetCamera();

  renderWindow->Render();
  interactor->Start();

  return EXIT_SUCCESS;
}



