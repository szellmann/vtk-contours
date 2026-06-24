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

inline vtkSmartPointer<vtkTexture> toLinearTexture(const std::vector<int64_t> &vec) {
  auto arr = vtkSmartPointer<vtkIntArray>::New();
  arr->SetNumberOfComponents(1);
  arr->SetNumberOfValues(vec.size());
  for (size_t i=0; i<vec.size(); ++i) {
    arr->SetValue(i, (int32_t)vec[i]);
  }

  auto i1D = vtkSmartPointer<vtkImageData>::New();
  i1D->SetDimensions(vec.size(), 1, 1);
  i1D->GetPointData()->SetScalars(arr);

  auto tex = vtkSmartPointer<vtkTexture>::New();
  tex->SetInputData(i1D);

  tex->InterpolateOff();
  tex->SetColorModeToDirectScalars();

  return tex;
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

  // serial contour tree:
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

    viskores::cont::ArrayHandle<viskores::Int64> nodesArray = contourTree.Nodes;
    viskores::cont::ArrayHandle<viskores::Int64> arcsArray = contourTree.Arcs;
    viskores::cont::ArrayHandle<viskores::Int64> superparentsArray = contourTree.Superparents;
    viskores::cont::ArrayHandle<viskores::Int64> superarcsArray = contourTree.Superarcs;
    viskores::cont::ArrayHandle<viskores::Int64> supernodesArray = contourTree.Supernodes;
    viskores::cont::ArrayHandle<viskores::Int64> hyperparentsArray = contourTree.Hyperparents;
    viskores::cont::ArrayHandle<viskores::Int64> whenTransferredArray = contourTree.WhenTransferred;
    viskores::cont::ArrayHandle<viskores::Int64> hypernodesArray = contourTree.Hypernodes;
    viskores::cont::ArrayHandle<viskores::Int64> hyperarcsArray = contourTree.Hyperarcs;

    nodes = toStdVector(nodesArray);
    arcs = toStdVector(arcsArray);
    superparents = toStdVector(superparentsArray);
    superarcs = toStdVector(superarcsArray);
    supernodes = toStdVector(supernodesArray);
    hyperparents = toStdVector(hyperparentsArray);
    whenTransferred = toStdVector(whenTransferredArray);
    hypernodes = toStdVector(hypernodesArray);
    hyperarcs = toStdVector(hyperarcsArray);
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

  vtkShaderProperty* shaderProperty = actor->GetShaderProperty();

  actor->GetProperty()->SetTexture("nodes", toLinearTexture(nodes));

  shaderProperty->SetFragmentShaderCode(R"(
//VTK::System::Dec
//VTK::Output::Dec
in vec2 tcoordVCVSOutput;
uniform sampler2D actortexture;
uniform isampler2D nodes;
void main() {
  vec4 texColor = texture(actortexture, tcoordVCVSOutput);
  gl_FragData[0] = texColor;
}
      )");

  auto renderer = vtkSmartPointer<vtkRenderer>::New();
  auto renderWindow = vtkSmartPointer<vtkRenderWindow>::New();
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



