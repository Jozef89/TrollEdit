#ifndef DOC_BLOCK_H
#define DOC_BLOCK_H

#include "block.h"

class TreeElement;
class DocumentScene;
class FoldButton;
class TextItem;
class Arrow;

class DocBlock : public Block
{
    Q_OBJECT
public:
    DocBlock(QPointF pos, BlockGroup *parentgroup = 0);
    ~DocBlock();

    enum { Type = UserType + 3 };
    void addText(QString text);
    void addImage(const QImage &image);
    void addFile(QUrl url);

protected:
    void focusOutEvent(QFocusEvent *event);
    void addArrow(DocBlock *start,Block *end, QGraphicsScene *parentScene);
    void updateBlock(bool doAnimation = true);
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget);

    QImage image;
    Arrow *arrow;

    friend class BlockGroup;
};

#endif // DOC_BLOCK_H
