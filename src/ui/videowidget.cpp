#include "videowidget.h"
#include "ui_videowidget.h"
#include <QPainter>
#include <QResizeEvent>
#include <QPaintEvent>
#include <QLinearGradient>
#include <QPainterPath>
#include <QFont>

VideoWidget::VideoWidget(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::VideoWidget)
{
    ui->setupUi(this);
    
    // Set widget properties
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(640, 480);
    
    // Basic widget setup
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoSystemBackground);
}

VideoWidget::~VideoWidget()
{
    delete ui;
}

bool VideoWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Show || event->type() == QEvent::Resize) {
        raise();
        if (QWidget *parentWidget = qobject_cast<QWidget*>(parent())) {
            parentWidget->raise();
        }
    }
    return QWidget::eventFilter(watched, event);
}

void VideoWidget::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    raise();
    if (QWidget *parentWidget = qobject_cast<QWidget*>(parent())) {
        parentWidget->raise();
    }
}

void VideoWidget::setFrame(const cv::Mat &frame)
{
    if (frame.empty()) return;

    // Convert BGR to RGB
    cv::Mat rgbFrame;
    cv::cvtColor(frame, rgbFrame, cv::COLOR_BGR2RGB);
    
    // Create QImage from frame data
    m_currentImage = QImage(rgbFrame.data, 
                          rgbFrame.cols, 
                          rgbFrame.rows, 
                          rgbFrame.step, 
                          QImage::Format_RGB888).copy();
    
    // Trigger repaint
    update();
}

void VideoWidget::clear()
{
    m_currentImage = QImage();
    update();
}

void VideoWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    // Create main widget path for clipping
    QPainterPath clipPath;
    clipPath.addRoundedRect(rect(), 12, 12);
    painter.setClipPath(clipPath);
    
    // Create gradient background
    QLinearGradient gradient(rect().topLeft(), rect().bottomLeft());
    gradient.setColorAt(0, QColor("#1A1A1A"));
    gradient.setColorAt(1, QColor("#252526"));
    painter.fillRect(rect(), gradient);
    
    if (!m_currentImage.isNull()) {
        // Calculate scaled size maintaining aspect ratio
        QSize scaledSize = m_currentImage.size();
        scaledSize.scale(size().width() - 16, size().height() - 16, Qt::KeepAspectRatio);
        
        // Calculate position to center the image
        QRect targetRect(QPoint(0, 0), scaledSize);
        targetRect.moveCenter(rect().center());
        
        // Draw the image with smooth scaling
        QImage scaledImage = m_currentImage.scaled(scaledSize, 
                                                 Qt::KeepAspectRatio, 
                                                 Qt::SmoothTransformation);
        painter.drawImage(targetRect, scaledImage);
    } else {
        // Draw placeholder text when no video
        painter.setPen(QPen(QColor("#666666")));
        painter.setFont(QFont("Arial", 12));
        painter.drawText(rect(), Qt::AlignCenter, "No Video Signal");
    }

    // Reset clip path for shadow and border
    painter.setClipPath(QPainterPath());
    
    // Create border path slightly inset
    QRect borderRect = rect().adjusted(1, 1, -1, -1);
    QPainterPath borderPath;
    borderPath.addRoundedRect(borderRect, 12, 12);
    
    // Draw shadow with proper clipping
    painter.setPen(Qt::NoPen);
    QPainterPath shadowPath;
    shadowPath.addRoundedRect(borderRect.translated(2, 2), 12, 12);
    painter.setBrush(QColor(0, 0, 0, 30));
    painter.drawPath(shadowPath);
    
    // Draw border
    painter.setPen(QPen(QColor("#3E3E3E"), 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(borderPath);
}

QSize VideoWidget::sizeHint() const
{
    if (!m_currentImage.isNull()) {
        return m_currentImage.size();
    }
    return QSize(640, 480);
}

void VideoWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    update();
} 