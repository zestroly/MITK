/*============================================================================

The Medical Imaging Interaction Toolkit (MITK)

Copyright (c) German Cancer Research Center (DKFZ)
All rights reserved.

Use of this source code is governed by a 3-clause BSD license that can be
found in the LICENSE file.

============================================================================*/

// Blueberry
#include <berryISelectionService.h>
#include <berryIWorkbenchWindow.h>
#include <berryIWorkbenchPage.h>

// mitk
#include <QmitkRenderWindow.h>
#include <mitkImagePixelReadAccessor.h>
#include <mitkPixelTypeMultiplex.h>

// Qt
#include <QMessageBox>
#include <QMessageBox>
#include <QFileDialog>
#include <qwt_plot_marker.h>

#include "QmitkDicomInspectorView.h"

const std::string QmitkDicomInspectorView::VIEW_ID = "org.mitk.gui.qt.dicominspector";

QmitkDicomInspectorView::ObserverInfo::ObserverInfo(mitk::SliceNavigationController* controller,
  int observerTag, const std::string& renderWindowName, mitk::IRenderWindowPart* part) : controller(controller), observerTag(observerTag),
  renderWindowName(renderWindowName), renderWindowPart(part)
{
}

QmitkDicomInspectorView::QmitkDicomInspectorView() :
m_renderWindowPart(nullptr),
m_PendingSliceChangedEvent(false),
m_currentSelectedTimeStep(0),
m_currentSelectedZSlice(0),
m_SelectedNode(nullptr),
m_internalUpdateFlag(false)
{
  m_currentSelectedPosition.Fill(0.0);
}

QmitkDicomInspectorView::~QmitkDicomInspectorView()
{
  this->RemoveAllObservers();
}

bool QmitkDicomInspectorView::InitObservers()
{
  bool result = true;

  typedef QHash<QString, QmitkRenderWindow*> WindowMapType;
  WindowMapType windowMap = m_renderWindowPart->GetQmitkRenderWindows();

  auto i = windowMap.begin();

  while (i != windowMap.end())
  {
    mitk::SliceNavigationController* sliceNavController =
      i.value()->GetSliceNavigationController();

    if (sliceNavController)
    {
      itk::ReceptorMemberCommand<QmitkDicomInspectorView>::Pointer cmdSliceEvent =
        itk::ReceptorMemberCommand<QmitkDicomInspectorView>::New();
      cmdSliceEvent->SetCallbackFunction(this, &QmitkDicomInspectorView::OnSliceChanged);
      int tag = sliceNavController->AddObserver(
        mitk::SliceNavigationController::GeometrySliceEvent(nullptr, 0),
        cmdSliceEvent);

      m_ObserverMap.insert(std::make_pair(sliceNavController, ObserverInfo(sliceNavController, tag,
        i.key().toStdString(), m_renderWindowPart)));

      itk::ReceptorMemberCommand<QmitkDicomInspectorView>::Pointer cmdTimeEvent =
        itk::ReceptorMemberCommand<QmitkDicomInspectorView>::New();
      cmdTimeEvent->SetCallbackFunction(this, &QmitkDicomInspectorView::OnSliceChanged);
      tag = sliceNavController->AddObserver(
        mitk::SliceNavigationController::GeometryTimeEvent(nullptr, 0),
        cmdTimeEvent);

      m_ObserverMap.insert(std::make_pair(sliceNavController, ObserverInfo(sliceNavController, tag,
        i.key().toStdString(), m_renderWindowPart)));

      itk::MemberCommand<QmitkDicomInspectorView>::Pointer cmdDelEvent =
        itk::MemberCommand<QmitkDicomInspectorView>::New();
      cmdDelEvent->SetCallbackFunction(this,
        &QmitkDicomInspectorView::OnSliceNavigationControllerDeleted);
      tag = sliceNavController->AddObserver(
        itk::DeleteEvent(), cmdDelEvent);

      m_ObserverMap.insert(std::make_pair(sliceNavController, ObserverInfo(sliceNavController, tag,
        i.key().toStdString(), m_renderWindowPart)));
    }

    ++i;

    result = result && sliceNavController;
  }

  return result;
}

void QmitkDicomInspectorView::RemoveObservers(const mitk::SliceNavigationController*
  deletedSlicer)
{

  std::pair < ObserverMapType::const_iterator, ObserverMapType::const_iterator> obsRange =
    m_ObserverMap.equal_range(deletedSlicer);

  for (ObserverMapType::const_iterator pos = obsRange.first; pos != obsRange.second; ++pos)
  {
    pos->second.controller->RemoveObserver(pos->second.observerTag);
  }

  m_ObserverMap.erase(deletedSlicer);
}

void QmitkDicomInspectorView::RemoveAllObservers(mitk::IRenderWindowPart* deletedPart)
{
  for (ObserverMapType::const_iterator pos = m_ObserverMap.begin(); pos != m_ObserverMap.end();)
  {
    ObserverMapType::const_iterator delPos = pos++;

    if (deletedPart == nullptr || deletedPart == delPos->second.renderWindowPart)
    {
      delPos->second.controller->RemoveObserver(delPos->second.observerTag);
      m_ObserverMap.erase(delPos);
    }
  }
}

void QmitkDicomInspectorView::OnSliceNavigationControllerDeleted(const itk::Object* sender,
  const itk::EventObject& /*e*/)
{
  const mitk::SliceNavigationController* sendingSlicer =
    dynamic_cast<const mitk::SliceNavigationController*>(sender);

  this->RemoveObservers(sendingSlicer);
}

void QmitkDicomInspectorView::RenderWindowPartActivated(mitk::IRenderWindowPart* renderWindowPart)
{
  if (m_renderWindowPart != renderWindowPart)
  {
    m_renderWindowPart = renderWindowPart;

    if (!InitObservers())
    {
      QMessageBox::information(nullptr, "Error", "Unable to set up the event observers. The " \
        "plot will not be triggered on changing the crosshair, " \
        "position or time step.");
    }
  }
}

void QmitkDicomInspectorView::RenderWindowPartDeactivated(
  mitk::IRenderWindowPart* renderWindowPart)
{
  m_renderWindowPart = nullptr;
  this->RemoveAllObservers(renderWindowPart);
}

void QmitkDicomInspectorView::CreateQtPartControl(QWidget* parent)
{
  // create GUI widgets from the Qt Designer's .ui file
  m_Controls.setupUi(parent);

  m_Controls.singleSlot->SetDataStorage(GetDataStorage());
  m_Controls.singleSlot->SetSelectionIsOptional(true);
  m_Controls.singleSlot->SetEmptyInfo(QString("Please select a data node"));
  m_Controls.singleSlot->SetPopUpTitel(QString("Select data node"));

  m_SelectionServiceConnector = std::make_unique<QmitkSelectionServiceConnector>();
  SetAsSelectionListener(true);

  m_Controls.timePointValueLabel->setText(QString(""));
  m_Controls.slieceNumberValueLabel->setText(QString(""));

  connect(m_Controls.singleSlot, &QmitkSingleNodeSelectionWidget::CurrentSelectionChanged,
    this, &QmitkDicomInspectorView::OnCurrentSelectionChanged);

  mitk::IRenderWindowPart* renderWindowPart = GetRenderWindowPart();
  RenderWindowPartActivated(renderWindowPart);
}

void QmitkDicomInspectorView::SetFocus()
{
}

void QmitkDicomInspectorView::OnCurrentSelectionChanged(QList<mitk::DataNode::Pointer> nodes)
{
  if (nodes.empty() || nodes.front().IsNull())
  {
    m_SelectedNode = nullptr;
    m_SelectedData = nullptr;
    UpdateData();
    return;
  }

  if (nodes.front() == this->m_SelectedNode)
  {
    // nothing to change
    return;
  }

  // node is selected, create DICOM tag table
  m_SelectedNode = nodes.front();
  m_SelectedData = this->m_SelectedNode->GetData();

  m_selectedNodeTime.Modified();
  UpdateData();
  OnSliceChangedDelayed();
}

void QmitkDicomInspectorView::ValidateAndSetCurrentPosition()
{
  mitk::Point3D currentSelectedPosition = GetRenderWindowPart()->GetSelectedPosition(nullptr);
  unsigned int currentSelectedTimeStep = GetRenderWindowPart()->GetTimeNavigationController()->GetTime()->GetPos();

  if (m_currentSelectedPosition != currentSelectedPosition
    || m_currentSelectedTimeStep != currentSelectedTimeStep
    || m_selectedNodeTime > m_currentPositionTime)
  {
    //the current position has been changed or the selected node has been changed since the last position validation -> check position
    m_currentSelectedPosition = currentSelectedPosition;
    m_currentSelectedTimeStep = currentSelectedTimeStep;
    m_currentPositionTime.Modified();
    m_validSelectedPosition = false;

    if (m_SelectedData.IsNull())
    {
      return;
    }

    mitk::BaseGeometry::Pointer geometry = m_SelectedData->GetTimeGeometry()->GetGeometryForTimeStep(
      m_currentSelectedTimeStep);

    // check for invalid time step
    if (geometry.IsNull())
    {
      geometry = m_SelectedData->GetTimeGeometry()->GetGeometryForTimeStep(0);
    }

    if (geometry.IsNull())
    {
      return;
    }

    m_validSelectedPosition = geometry->IsInside(m_currentSelectedPosition);
    itk::Index<3> index;
    geometry->WorldToIndex(m_currentSelectedPosition, index);

    m_currentSelectedZSlice = index[2];
  }
}

void QmitkDicomInspectorView::OnSliceChanged(const itk::EventObject&)
{
  // Taken from QmitkStdMultiWidget::HandleCrosshairPositionEvent().
  // Since there are always 3 events arriving (one for each render window) every time the slice
  // or time changes, the slot OnSliceChangedDelayed is triggered - and only if it hasn't been
  // triggered yet - so it is only executed once for every slice/time change.
  if (!m_PendingSliceChangedEvent)
  {
    m_PendingSliceChangedEvent = true;

    QTimer::singleShot(0, this, SLOT(OnSliceChangedDelayed()));
  }
}

void QmitkDicomInspectorView::OnSliceChangedDelayed()
{
  m_PendingSliceChangedEvent = false;

  ValidateAndSetCurrentPosition();

  m_Controls.tableTags->setEnabled(m_validSelectedPosition);

  if (m_SelectedNode.IsNotNull())
  {
    RenderTable();
  }
}

void QmitkDicomInspectorView::RenderTable()
{
  assert(m_renderWindowPart != nullptr);

  // configure fit information
  unsigned int rowIndex = 0;
  for (const auto& element : m_Tags)
  {
    QTableWidgetItem* newItem = new QTableWidgetItem(QString::fromStdString(element.second.prop->GetValue(m_currentSelectedTimeStep, m_currentSelectedZSlice, true, true)));
    m_Controls.tableTags->setItem(rowIndex, 3, newItem);
    ++rowIndex;
  }

  UpdateLabels();
}

void QmitkDicomInspectorView::UpdateData()
{
  QStringList headers;

  m_Controls.tableTags->horizontalHeader()->resizeSections(QHeaderView::ResizeToContents);

  m_Tags.clear();

  if (m_SelectedData.IsNotNull())
  {
    for (const auto& element : *(m_SelectedData->GetPropertyList()->GetMap()))
    {
      if (element.first.find("DICOM") == 0)
      {
        std::istringstream stream(element.first);
        std::string token;
        std::getline(stream, token, '.'); //drop the DICOM suffix
        std::getline(stream, token, '.'); //group id
        unsigned long dcmgroup = std::stoul(token, nullptr, 16);
        std::getline(stream, token, '.'); //element id
        unsigned long dcmelement = std::stoul(token, nullptr, 16);

        TagInfo info(mitk::DICOMTag(dcmgroup, dcmelement), dynamic_cast<const mitk::DICOMProperty*>(element.second.GetPointer()));

        m_Tags.insert(std::make_pair(element.first, info));
      }
    }
  }

  m_Controls.tableTags->setRowCount(m_Tags.size());

  unsigned int rowIndex = 0;
  for (const auto& element : m_Tags)
  {
    QTableWidgetItem* newItem = new QTableWidgetItem(QString::number(element.second.tag.GetGroup(), 16));
    m_Controls.tableTags->setItem(rowIndex, 0, newItem);
    newItem = new QTableWidgetItem(QString::number(element.second.tag.GetElement(), 16));
    m_Controls.tableTags->setItem(rowIndex, 1, newItem);
    newItem = new QTableWidgetItem(QString::fromStdString(element.second.tag.GetName()));
    m_Controls.tableTags->setItem(rowIndex, 2, newItem);
    newItem = new QTableWidgetItem(QString::fromStdString(element.second.prop->GetValue()));
    m_Controls.tableTags->setItem(rowIndex, 3, newItem);
    ++rowIndex;
  }
  UpdateLabels();
}

void QmitkDicomInspectorView::UpdateLabels()
{
  if (m_SelectedData.IsNull())
  {
    if (m_SelectedNode.IsNotNull())
    {
      //m_Controls.labelNode->setText(QString("<font color='red'><b>INVALIDE NODE</font>"));
    }
    else
    {
      //m_Controls.labelNode->setText(QString("select node..."));
    }
    m_Controls.timePointValueLabel->setText(QString(""));
    m_Controls.slieceNumberValueLabel->setText(QString(""));
  }
  else
  {
    if (m_validSelectedPosition)
    {
      m_Controls.timePointValueLabel->setText(QString::number(m_currentSelectedTimeStep));
      m_Controls.slieceNumberValueLabel->setText(QString::number(m_currentSelectedZSlice));
    }
    else
    {
      m_Controls.timePointValueLabel->setText(QString("outside data geometry"));
      m_Controls.slieceNumberValueLabel->setText(QString("outside data geometry"));
    }
  }
}

void QmitkDicomInspectorView::SetAsSelectionListener(bool checked)
{
  if (checked)
  {
    m_SelectionServiceConnector->AddPostSelectionListener(GetSite()->GetWorkbenchWindow()->GetSelectionService());
    connect(m_SelectionServiceConnector.get(), &QmitkSelectionServiceConnector::ServiceSelectionChanged,
      m_Controls.singleSlot, &QmitkSingleNodeSelectionWidget::SetCurrentSelection);
  }
  else
  {
    m_SelectionServiceConnector->RemovePostSelectionListener();
    disconnect(m_SelectionServiceConnector.get(), &QmitkSelectionServiceConnector::ServiceSelectionChanged,
      m_Controls.singleSlot, &QmitkSingleNodeSelectionWidget::SetCurrentSelection);
  }
}
