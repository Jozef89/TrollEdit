#include "block.h"
#include "hide_block_button.h"
#include "text_item.h"
#include "../analysis/tree_element.h"
#include "../widget/document_scene.h"

int Block::OFFSH = 10;
int Block::OFFSV = 3;
QMap<int, Block*> Block::lineStarts;
int Block::lastLine = 0;
static int lastX = -1;

Block::Block(TreeElement *element, Block *parentBlock, QGraphicsScene *parentScene)
    : QGraphicsRectItem(parentBlock)
{
    if (parentBlock == 0) { // adding directly to scene, no parent block
        parentScene->addItem(this);
        docScene = (DocumentScene*)parentScene;
        parent = 0;
        prevSib = 0;
    } else {
        parent = parentBlock;
        docScene = parent->docScene;

        QList<Block*> siblings = parent->childBlocks();
        if (siblings.size() > 1) {      // this block is in siblings already!
            prevSib = siblings.at(siblings.size() - 2);
            prevSib->nextSib = this;
        } else {
            parent->firstChild = this;
            prevSib = 0;
        }
        if (element->getParent() == 0) {
            parentBlock->element->appendChild(element);
        }
    }
    firstChild = 0;
    nextSib = 0;
    line = 0;

    while (!element->isImportant()) // skip "unimportant" elements
        element = (*element)[0];

    this->element = element;
    element->setBlock(this);

    if (element->isLeaf()) {
        myTextItem = new TextItem(element->getType(), this, element->allowsParagraphs());
        myTextItem->setPos(OFFSH-4, 0);
        if (element->getParent() != 0)
            setToolTip(element->getParent()->getType());

        // highlight text
//        if (docScene->getHighlightning().find(element->getParent()->getType()) != docScene->getHighlightning().end()) {
//            QPair<QFont, QColor> pair = docScene->getHighlightning().value(element->getParent()->getType());
//            myTextItem->font().setBold(pair.first.bold());
//            myTextItem->font().setItalic(pair.first.italic());
//            myTextItem->font().setUnderline(pair.first.underline());
//            myTextItem->setDefaultTextColor(pair.second);
//        }
    } else {
        myTextItem = 0;
        setToolTip(element->getType());
        foreach (TreeElement *childEl, element->getChildren()) {
            new Block(childEl, this);
        }
    }
    createControls();
    
    setFlag(QGraphicsItem::ItemIsMovable);
    setFlag(QGraphicsItem::ItemIsFocusable);

    folded = false;
    pressed = false;
    edited = false;
    selected = false;
//    setAcceptHoverEvents(true);
}

Block::~Block()
{
    delete(element);
}
void Block::removeLinks()
{
    if (nextSib != 0) {
        nextSib->prevSib = prevSib;
        if (prevSib == 0 && nextSib->getSpaces() > 0)
            nextSib->element->setSpaces(0);
    }
    if (prevSib != 0) {
        prevSib->nextSib = nextSib;
        if (nextSib == 0 && prevSib->element->isLineBreaking()) {
            prevSib->element->setLineBreaking(false);
            if (parent != 0)
                parent->element->setLineBreaking(true);
        }
    }
    if (parent != 0 && parent->firstChild == this) parent->firstChild = nextSib;
}

void Block::createControls()
{
    //    if (element->isMultiLine()) {
    //        hideButton = new HideBlockButton(this);
    //        hideButton->setPos(0,0);
    //        hideButton->setVisible(false);
    //        setAcceptHoverEvents(true);
    //    } else
    hideButton = 0;
}

void Block::setParentItem(QGraphicsItem *parentItem)
        // moves this element and all unimportant elements on way to parentBlock's element ("branch")
        // to new parent
        // NOTE: to remove block from its parent use removeBlock() which removes all empty ancestors too
{
    if (parentItem == this) {
        parentItem = 0;//debug - toto nemoze nastavat
    }
    TreeElement *branch;
    // remove from old parent element
    Block *oldParent = this->parent;
    if (oldParent != 0) {
        TreeElement *oldParentEl = oldParent->element;
        // find ancestor that is child of oldParentEl (i.e. root of the branch)
        int index = oldParentEl->indexOfBranch(this->element);
        branch = (*oldParentEl)[index];
        // remove branch from original parent
        oldParentEl->removeChild(branch);
        // remove spaces
        element->setSpaces(0);
        // remove links
        removeLinks();
    } else {
        branch = element->getRoot(); // get root of unvisualized(?) branch
    }
    // append to new parent element
    Block *newParent = qgraphicsitem_cast<Block*>(parentItem);
    if (newParent != 0) {
        TreeElement *newParentEl = newParent->element;
        newParentEl->appendChild(branch);
        branch = 0;
        // update links
        QList<Block*> siblings = newParent->childBlocks();
        if (siblings.size() > 0) {      // this block is not in siblings yet!
            prevSib = siblings.at(siblings.size() - 1);
            prevSib->nextSib = this;
        } else {
            newParent->firstChild = this;
            prevSib = 0;
        }
        nextSib = 0;
    }

    // remove from old parent block & append to new parent block
    QGraphicsRectItem::setParentItem(parentItem);
    this->parent = newParent;
}

void Block::stackBefore(const QGraphicsItem *sibling)
        // moves this element and all unimportant elements on way to parentBlock's element ("branch")
        // within current parent of this branch
{
    Block *nextSibling = qgraphicsitem_cast<Block*>(const_cast<QGraphicsItem*>(sibling));
    if (nextSibling != 0 && nextSibling != nextSib) {
        if (parent != 0 && nextSibling->parent == parent) {
            TreeElement *parentEl = parent->element;
            // find ancestor that is child of parentEl (i.e root of the branch)
            int index = parentEl->indexOfBranch(this->element);
            TreeElement *branch = (*parentEl)[index];
            // remove branch from original position
            parentEl->removeChild(branch);
            // compute new index
            index = parentEl->indexOfBranch(nextSibling->element);
            // insert branch at new position
            parentEl->insertChild(index, branch);
            // update links
            removeLinks();
            prevSib = nextSibling->prevSib;
            if (prevSib != 0)
                prevSib->nextSib = this;
            else
                parent->firstChild = this;
            nextSib = nextSibling;
            nextSibling->prevSib = this;
        }
    }
    QGraphicsRectItem::stackBefore(sibling);
}

QList<Block*> Block::removeBlock()
{
    int remSpaces = 0;
    QList<Block*> toDelete;
    Block *block = this;
    do {    // remove this block and all ancestors (that would became leafs) from hierarchy
        remSpaces += block->getSpaces();    // collect spaces from deleted blocks
        Block *oldParent = block->parent;
        block->setParentItem(0);
        toDelete << block;
        block = oldParent;
    } while (block != 0 && block->element->isLeaf());
    toDelete.removeOne(this);
    element->setSpaces(remSpaces);
    return toDelete;
}

Block *Block::parentBlock() const
{
    return parent;
}
TreeElement *Block::getElement() const
{
    return element;
}
QList<Block*> Block::childBlocks() const
{
    return blocklist_cast(childItems());
}
TextItem *Block::textItem() const
{
    return myTextItem;
}

Block *Block::getFirstLeaf() const
{
    if (firstChild == 0) return const_cast<Block*>(this);
    Block *block = firstChild;
    while (!block->isTextBlock())
        block = block->firstChild;
    return block;
}
Block *Block::getLastLeaf() const
{
    if (firstChild == 0) return const_cast<Block*>(this);
    Block *block = childBlocks().last();
    while (!block->isTextBlock())
        block = block->childBlocks().last();
    return block;
}

Block *Block::getAncestorWhereFirst() const
{
    Block *block = const_cast<Block*>(this);
    while (block->prevSib == 0 && block->parent != 0)
        block = block->parent;
    return block;
}
Block *Block::getNextSibling() const
{
    return nextSib;
}
Block *Block::getNext(bool textOnly) const
{
    Block *next = const_cast<Block*>(this);
    if (parent != 0) {
        if (nextSib == 0)
            return parent->getNext(textOnly);
        next = nextSib;
    }
    if (textOnly)
        return next->getFirstLeaf();
    return next;
}
Block *Block::getPrev(bool textOnly) const
{
    Block *prev = const_cast<Block*>(this);
    if (parent != 0) {
        if (prevSib == 0)
            return parent->getPrev(textOnly);
        prev = prevSib;
    }
    if (textOnly) {
        return prev->getLastLeaf();
    }
    return prev;
}

int Block::numberOfLines() const
{
    if (isTextBlock()) {
        return 1;
    } else {
        Block *last = childBlocks().last();
        return (last->line + last->numberOfLines() - 1) - this->line + 1;
    }
}
bool Block::hasMoreLines() const
{
    if (isTextBlock()) {
        return false;
    } else {
        Block *last = childBlocks().last();
        return (last->line > this->line) || last->hasMoreLines();
    }
}
int Block::computeNextSiblingLine() const
{
    int lineNo = line + numberOfLines() - 1;
    if (element->isLineBreaking()) lineNo++;
    return lineNo;
}

qreal SPACE_WIDTH = 10; // temp
QPointF Block::computeNextSiblingPos() const
{
    if (parent == 0)
        return QPointF();
    QPointF position = pos();
    if (!element->isLineBreaking()) {
        if (hasMoreLines()) {
            Block *block = getLastLeaf();
            position = parent->mapFromItem(block, block->boundingRect().topRight());
            position.rx() += OFFSH/3;
        } else {
            position.rx() += OFFSH/3 + boundingRect().width();
        }
    } else {
        position.rx() = OFFSH;
        qreal maxY = 0;
        foreach (Block *child, parent->childBlocks()) {
            maxY = qMax(maxY, child->pos().y() + child->boundingRect().bottom());
            //            if (child->element->isLineBreaking() && child != this)
            //                maxY = 0;
            if (child == this) break;
        }
        position.ry() = OFFSV + maxY;
    }
    return position;
}

void Block::focusInEvent(QFocusEvent *event)
{
}
void Block::focusOutEvent(QFocusEvent *event)
{
}
void Block::textFocusChanged(QFocusEvent* event)
{
    if (event->lostFocus())
        focusOutEvent(event);
}
void Block::textChanged()
{
    QString text = myTextItem->toPlainText();
    myTextItem->document()->blockSignals(true);
    if (text.isEmpty()) {   // delete block
        if (!element->isLineBreaking() || getPrev()->line == line) {
            // don't delete if block is single newline in this line

            Block *next = getNext();
            QList<Block*> toDelete = removeBlock();

            if (!toDelete.contains(next)) {
                if (next->line > line) { // jumped to next line
                    Block *prev = next->getPrev(true);
                    if (prev->line > line) {  // jumped to the end of file
                        next->getFirstLeaf()->textItem()->setTextCursorPosition(0);
                    } else {                  // same line
                        prev->textItem()->setTextCursorPosition(-1);
//                        prev->element->setLineBreaking(true);
                    }
                    next->updateAfter(true);
                } else if (next->line < line) { // jumped to the beginning of file
                    next->getPrev(true)->textItem()->setTextCursorPosition(-1);
                } else {                         // on same line
                    next->element->addSpaces(element->getSpaces());
                    next->getFirstLeaf()->textItem()->setTextCursorPosition(0);
                    next->updateAfter(true);
                }
            }
            docScene->update();

            deleteLater();
            foreach (Block* block, toDelete) block->deleteLater();
            // it is very important to call deleteLater() only AFTER update() !!
            return;
        }
    }
    if (element->getType() != text) {
        edited = true;
        lastX = -1;
    }
    if (text.startsWith(" ")) { // remove leading spaces
        Block *ancestor = getAncestorWhereFirst();
        do {
            text.remove(0, 1);
            ancestor->element->addSpaces(1);
        } while(text.startsWith(" "));
        element->setType(text);
        myTextItem->setPlainText(text);
        ancestor->updateAfter(true);
    } else {
        element->setType(text);
        updateXPosInLine(line);
    }

    docScene->update();
    myTextItem->document()->blockSignals(false);
}

void Block::keyPressed(QKeyEvent* event)
{
    if (event->key() != Qt::Key_Up && event->key() != Qt::Key_Down)
        lastX = -1;
}

void Block::splitLine(int cursorPos)
{
    if (parent == 0) return;
    if (element->allowsParagraphs()) {      // no splitting is done
        updateAfter();
        return;
    }
    // check what block should be splitted
    if (cursorPos == 0) {
        Block *block = getPrev();           // split previous block
        if (block->parent != 0)
            block->splitLine();
        return;
    } else if ((cursorPos == length() || cursorPos == -1) && nextSib == 0) {
        Block *block = getNext();           // split next block
        if (block->parent != 0) {
            block->getPrev()->splitLine();
        }
        return;
    } else { // split this block
        // update this block
        QString text = "";
        if (cursorPos >= 0) {
            text = textItem()->toPlainText();
            textItem()->setPlainText(text.left(cursorPos));
            text.remove(0,cursorPos);
        }
        bool alreadyBreaking = !this->element->setLineBreaking(true);

        Block *next = getNext();
        // create new block (either with text or with newline)
        if (!text.isEmpty() || alreadyBreaking) {
            Block *newBlock = new Block(new TreeElement(text), parent);
            newBlock->element->setLineBreaking(alreadyBreaking);
            newBlock->stackBefore(next);
            newBlock->textItem()->setTextCursorPosition(0);
        } else {
            next->element->setSpaces(0);
            next->getFirstLeaf()->textItem()->setTextCursorPosition(0);
        }
        updateAfter(true);
        docScene->update();
    }
}
void Block::eraseChar(int key) {
    Block *target = 0;
    if (key == Qt::Key_Backspace) {          // move to previous block
        target = getAncestorWhereFirst();
        if (target->getSpaces() > 0) {
            target->element->addSpaces(-1);
            target->updateAfter(true);
        } else {
            target = getPrev(true);
            if (target->line < line) {          // jumped to previous line
                while (!target->element->isLineBreaking())
                    target = target->parent;
                target->element->setLineBreaking(false);
                target->updateAfter();
                scene()->update();
                if (target->isTextBlock()) target->textChanged();
            } else if (target->line > line) {   // jumped to the end of file
                return;
            } else {                            // on same line
                target->textItem()->removeCharAt(-1);
            }
        }
        textItem()->setTextCursorPosition(0);
    } else if (key == Qt::Key_Delete) {     // move to next block
        target = getNext();
        if (target->getSpaces() > 0) {
            target->element->addSpaces(-1);
            updateXPosInLine(line);
        } else {
            target = getNext(true);
            if (target->line > line) {          // jumped to next line
                target = this;
                while (!target->element->isLineBreaking())
                    target = target->parent;
                target->element->setLineBreaking(false);
                target->updateAfter();
                scene()->update();
                if (target->isTextBlock()) target->textChanged();
            } else if (target->line < line) {   // jumped to the beginning of file
                return;
            } else {                            // on same line
                target->textItem()->removeCharAt(0);
            }
        }
        textItem()->setTextCursorPosition(-1);
    }

}
void Block::moveCursorLR(int key)
{
    Block *target = 0;
    int position;
    if (key == Qt::Key_Left) {          // move to previous block
        target = getPrev(true);
        position = -2;
        if (target->line != line)
            position = -1;
        if (getAncestorWhereFirst()->getSpaces() > 0)
            position = -1;
    } else if (key == Qt::Key_Right) {  // move to next block
        target = getNext(true);
        position = 1;
        if (target->line != line)
            position = 0;
        if (target->getAncestorWhereFirst()->getSpaces() > 0)
            position = 0;
    } else return;
    target->textItem()->setTextCursorPosition(position);
    lastX = -1;
}
void Block::moveCursorUD(int key, int from)
{// nedokoncene
    Block *lineBl = lineStarts[line]->getFirstLeaf();
    int mySpaces = lineBl->getAbsoluteSpaces() - lineBl->getAncestorWhereFirst()->getSpaces();
    int x = from + mySpaces + getAncestorWhereFirst()->element->getSpaces();// todo prerobit
//    if (getAncestorWhereFirst() != lineBl) x += getAncestorWhereFirst()->element->getSpaces()
    int y = line;

    // compute x
    if (lastX < 0) {
        while (lineBl != this) {
            x += lineBl->length() + lineBl->getAncestorWhereFirst()->getSpaces();
            lineBl = lineBl->getNext(true);
        }
        lastX = x;
    } else {
        x = lastX;
    }

    // compute y
    if (key == Qt::Key_Up) {            // move up
        if (line == 0)
        {y = lastLine;}
        else
            y = line - 1;
    } else if (key == Qt::Key_Down) {   // move down
        if (line == lastLine)
            y = 0;
        else
            y = line + 1;
    } else return;
    // move x characters in line y
    Block *target = lineStarts[y]->getFirstLeaf();
    int whites = target->getAbsoluteSpaces();
    while (true){
        int le = target->length() + whites;
        if (le >= x) {
            target->textItem()->setTextCursorPosition(qMax(0, x - whites));
            return;
        } else {
            x -= le;
        }
        Block *next = target->getNext(true);
        if (next->line != y) {
            target->textItem()->setTextCursorPosition(-1);
            return;
        }
        target = next;
        whites = target->getAncestorWhereFirst()->getSpaces();
    }
}

void Block::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    if (!QRectF(0,0,OFFSH,boundingRect().height()).contains(event->pos())) {
        // create new block
        //        Block *block = new Block(new TreeElement(" ",false, true), this);
        //        Block *next = findNextChildAt(event->pos());
        //        block->stackBefore(next);
        //        block->updatePos();
        //        block->setFocus();
        //        QGraphicsRectItem::mousePressEvent(event);
        return;
    }

    if (event->button() == Qt::LeftButton) {
        Block *oldParent = parent;
        // remove from parent and add directly to scene
        // item is now on top of everything (that have z-value==0)
        // new parent will be resolved after mouse is released
        QList<Block*> toDelete;
        if (oldParent != 0) {
            Block *next = getNext();
            setPos(scenePos());
            toDelete = removeBlock();
            if (next!= 0)
                next->updateAfter(true);  // update blocks after removal
            oldParent->edited = true;
        }
        pressed = true;
        edited = true;
        setZValue(100);
        QPointF curPos = pos();
        mouseMoveEvent(event);  // updates futureParent/Sibling and draw separator line immediatelly
        setPos(curPos);
        docScene->update();
        foreach(Block* block, toDelete) block->deleteLater();
    }
    QGraphicsRectItem::mousePressEvent(event);
}
void Block::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
//    if (!pressed) return;

    futureParent = 0;
    // target is under left top corner of moved block OR target is under cursor
    QList<Block*> blocks = blocklist_cast(docScene->items(/*event->*/scenePos()));
    foreach (Block *block, blocks) { // find next different block
        if (block != this) {
            futureParent = block;
            break;
        }
    }
    if (futureParent != 0 && futureParent->element->isLeaf()) {// leaf test - cannot add child to leaf
        futureParent = futureParent->parentBlock();
        if (futureParent == this) {
            futureParent = 0;//debug - toto nemoze nastavat
        }
    }
    if (futureParent != 0) {
        futureSibling = futureParent->findNextChildAt(futureParent->mapFromScene(/*event->*/scenePos()));
        docScene->showInsertLine(futureParent->getInsertLineAt(futureSibling));
    } else {
        futureSibling = 0;
        docScene->hideInsertLine();
    }
    QGraphicsRectItem::mouseMoveEvent(event);
}
void Block::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
//    if (!pressed) return;

    if (event->button() == Qt::LeftButton) {
        setZValue(0);               // restore z-value

        if (futureParent != 0) {
            setParentItem(futureParent);
            if (futureSibling != 0) {
                stackBefore(futureSibling);
            }
            futureParent->prepareGeometryChange();    // used to update graphics
            futureParent->edited = true;
            updateAfter(true);
        }
        futureParent = 0;
        futureSibling = 0;
        docScene->hideInsertLine();
    }
    pressed = false;
    QGraphicsRectItem::mouseReleaseEvent(event);
    docScene->update();
}

Block* Block::findNextChildAt(QPointF pos) const
{// note: distance is computed from pot left corner of block's bounding rect
    QList<Block*> blocks = childBlocks();
    if (blocks.isEmpty())
        return 0;

    Block *nextBlock = 0;
    QLineF dist = QLineF(QPointF(), pos);
    qreal minDist = dist.length();
    // test distance from block starts
    foreach (Block *block, blocks) {
        dist.setP1(mapFromItem(block, block->boundingRect().topLeft()));
        if (dist.length() < minDist) {
            minDist = dist.length();
            nextBlock = block;
        }
    }
    // test distance from last block end
    Block *lastBlock = blocks.last();
    dist.setP1(mapFromItem(lastBlock, lastBlock->boundingRect().topRight()));
    if (dist.length() < minDist) {
        nextBlock = 0;
    }

    return nextBlock;
}

QLineF Block::getInsertLineAt(const Block* nextBlock) const
{
    QLineF iLine;
    if (nextBlock != 0) {   // before child if provided
        QRectF rect = nextBlock->mapRectToScene(nextBlock->boundingRect());
        if (element->isLineBreaking() &&            // horizontal line
                (nextBlock->prevSib == 0 || nextBlock->prevSib->element->isLineBreaking()))
            iLine = QLineF(rect.topLeft(), rect.topRight());
        else                                        // vertical line
            iLine = QLineF(rect.topLeft(), rect.bottomLeft());
//        iLine.translate(-OFFSH/2, 0);
    } else {                // after child if not provided
        Block *lastChild = childBlocks().last();// must have at least 1 child
        QRectF rect = lastChild->mapRectToScene(lastChild->boundingRect());
        if (lastChild->element->isLineBreaking())   // horizontal line
            iLine = QLineF(rect.bottomLeft(), rect.bottomRight());
        else                                        // vertical line
            iLine = QLineF(rect.topRight(), rect.bottomRight());
//        iLine.translate(OFFSH/2, 0);
    }
    return iLine;
}

void Block::hoverEnterEvent(QGraphicsSceneHoverEvent *event)
{
    if (element->isSelectable())
        QGraphicsRectItem::hoverEnterEvent(event);
    selected = true;
}
void Block::hoverLeaveEvent(QGraphicsSceneHoverEvent *event)
{
    if (element->isSelectable())
        QGraphicsRectItem::hoverEnterEvent(event);
    selected = false;
}
void Block::hoverMoveEvent(QGraphicsSceneHoverEvent *event)
{
    //QGraphicsRectItem::hoverMoveEvent(event);
}

void Block::contextMenuEvent(QGraphicsSceneContextMenuEvent *event)
{
}
void Block::dragEnterEvent(QGraphicsSceneDragDropEvent *event)
{
}
void Block::dragLeaveEvent(QGraphicsSceneDragDropEvent *event)
{
}
void Block::dragMoveEvent(QGraphicsSceneDragDropEvent *event)
{
}

void Block::updateLayout() // parent to child updater
        // used to update everything from root up
        // updates line numbers
        // used when new root is created
{
    int lineNo = line;
    QPointF nextPos = QPointF(OFFSH, OFFSV);
    bool newLineComming = true;
    
    foreach (Block *child, childBlocks()) {
        child->line = lineNo;
        child->updateLayout();

        if (newLineComming) {
            lineStarts[lineNo] = child;
            newLineComming = false;
        }

        nextPos.rx() += child->getSpaces() * SPACE_WIDTH;
        child->setPos(nextPos);
        lastLine = qMax(lastLine, lineNo);

        lineNo = child->computeNextSiblingLine();
        nextPos = child->computeNextSiblingPos();
        if (child->element->isLineBreaking())
            newLineComming = true;
    }
}

void Block::updateAfter(bool updateThis) // child to parent updater
        // used to update everything after this block, after ins parent etc.,
        // updates line numbers
        // used after blocks moving or typing newlines
{
    if (parent == 0) return;
    int lineNo;
    QPointF nextPos;
    Block *sibling;
    bool newLineComming = false;
    
    if (updateThis) {   // start with this block
        sibling = prevSib;
    } else {            // start with next block
        if (nextSib == 0) {// no more siblings, update after parent
            parent->updateAfter();
            return;
        }
        sibling = this;
    }
    // initialize lineNo and NextPos
    if (sibling != 0) {
        lineNo = sibling->computeNextSiblingLine();
        nextPos = sibling->computeNextSiblingPos();
        if (sibling->element->isLineBreaking())
            newLineComming = true;
        sibling = sibling->nextSib;
    } else {
        lineNo = parent->line;
        nextPos = QPointF(OFFSH, OFFSV);
        sibling = this;
    }
    // update siblings
    while (sibling != 0) {
        nextPos.rx() +=  sibling->getSpaces() * SPACE_WIDTH;

        sibling->setLine(lineNo);
        sibling->setPos(nextPos);
        if (newLineComming) {
            lineStarts[lineNo] = sibling;
            newLineComming = false;
        }
        lastLine = lineNo;
        if (sibling->hasMoreLines())
            sibling->updateLineStarts();

        lineNo = sibling->computeNextSiblingLine();
        nextPos = sibling->computeNextSiblingPos();
        if (sibling->element->isLineBreaking())
            newLineComming = true;
        sibling = sibling->nextSib;
    }
    // this block and its siblings are updated, repeat with parent
    updateThis = parent->firstChild == this && updateThis;
    parent->updateAfter(updateThis);
}
void Block::updateLineStarts()
{
    Block *child = firstChild;
    bool newLineComming  = false;
    while (child != 0) {
        if (newLineComming) {
            lineStarts[child->line] = child;
            lastLine = child->line;
            newLineComming = false;
        }
        if (child->hasMoreLines())
            child->updateLineStarts();
        if (child->element->isLineBreaking())
            newLineComming = true;
        child = child->nextSib;
    }
}

void Block::updateXPosInLine(int lineNo) // child to parent updater
        // used to update everything from changed child down, updates only this line
        // doesn't update this block's position!
        // used after user's typing (without newlines)
{    
    if (parent == 0) return;
    qreal nextX = computeNextSiblingPos().x();
    Block *sibling = nextSib;
    while (sibling != 0) { // start with next block
        nextX +=  sibling->getSpaces() * SPACE_WIDTH;
        //        nextX = parent->mapFromScene(nextX, 0).x();

        sibling->setX(nextX);
        nextX = sibling->computeNextSiblingPos().x();
        sibling = sibling->nextSib;
    }
    // this block and its siblings are updated, repeat with parent
    //    if (parent->line == line)
    parent->updateXPosInLine(lineNo);
}

int Block::type() const
{
    return Type;
}

QRectF Block::boundingRect() const
{
    QRectF rect;
    if (isTextBlock()) {
        rect = myTextItem->mapRectToParent(myTextItem->boundingRect());
        // NOTE: returned rect if 1 pixel wider than needed (to draw cursor at the end)
        rect.adjust(-OFFSH, 0, -1, 0);
    } else {
        rect = childrenBoundingRect();
        rect.adjust(0, 0, OFFSH/3, OFFSV);
        rect.setTopLeft(QPointF());
    }

    return rect;
}

QPainterPath Block::shape() const   // default implementation
{
    QPainterPath path;
    path.addRect(boundingRect());
    return path;
}

void Block::paint(QPainter *painter,
                  const QStyleOptionGraphicsItem *option,
                  QWidget *widget)
{
    QRectF rect = boundingRect();
    painter->setPen(pen());
    painter->fillRect(rect, Qt::white);
    
    QColor color = Qt::lightGray;
    if (edited) color = Qt::yellow;
    painter->fillRect(QRectF(0,0,OFFSH,rect.height()), color);

    //*****
    if (element->isLineBreaking())
        painter->fillRect(QRectF(0,0,OFFSH,OFFSH), Qt::blue);
    if (element->allowsParagraphs())
        painter->fillRect(QRectF(0,OFFSH,OFFSH,OFFSH), Qt::green);
    if (OFFSH>0) {
        painter->drawText(3, OFFSH+10, QString("%1").arg(line));
        painter->drawText(3, OFFSH, QString("%1").arg(getSpaces()));
    }
    if (!element->isSelectable())
        painter->setPen(Qt::gray);
    if (element->isUnknown())
        painter->setPen(Qt::red);
    if (selected)
        painter->setPen(Qt::green);
    //*****

    painter->drawRect(rect);
}

void Block::setFolded(bool fold)
{
    if (fold == folded) return; // do nothing
    if (fold) {
        ;// todo fold
    } else {
        ;//todo unfold
    }
    foreach (Block *child, childBlocks()) {    // hide/unhide children
        if (child->line > 0) {
            child->setVisible(!fold);
        }
    }
    folded = fold;                          // update folded flag
}
bool Block::isFolded() const
{
    return folded;
}
void Block::setLine(int newLine)
{
    if (line == newLine)
        return;
    int diff = newLine - line;
    lastLine = line = newLine;
    foreach (Block *child, childBlocks()) {
        child->setLine(child->line + diff);
    }
}
int Block::getSpaces() const
{
    return element->getSpaces();
}
int Block::getAbsoluteSpaces() const
{
    if (parent == 0)
        return getSpaces();
    else
        return getSpaces() + parent->getAbsoluteSpaces();
}

bool Block::isTextBlock() const
{
    return myTextItem != 0;
}
int Block::length() const
{
    if (!isTextBlock()) return 0;
    return element->getType().length();
}

QList<Block*> Block::blocklist_cast(QList<QGraphicsItem*> list) const
{
    QList<Block*> blocks;
    foreach (QGraphicsItem *item, list) {
        Block *block = qgraphicsitem_cast<Block*>(item);
        if (block != 0)
            blocks << block;
    }
    return blocks;
}

