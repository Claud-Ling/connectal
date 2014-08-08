#ifndef _RBM_H_
#define _RBM_H_

#include "RbmRequestProxy.h"
#include "RbmIndicationWrapper.h"
#include "SigmoidRequestProxy.h"
#include "SigmoidIndicationWrapper.h"

class SigmoidIndication;

extern RbmRequestProxy *rbmdevice;
extern SigmoidRequestProxy *sigmoiddevice;
extern SigmoidIndication *sigmoidindication;

class RbmIndication : public RbmIndicationWrapper
{
public:
 RbmIndication(int id) : RbmIndicationWrapper(id) {
  }
  virtual ~RbmIndication() {}
  virtual void bramMmfDone() {
    //fprintf(stderr, "bramMmfDone\n");
    sem_post(&mul_sem);
  }
  virtual void toBramDone() {
    //fprintf(stderr, "toBramDone\n");
    sem_post(&mul_sem);
  }
  virtual void fromBramDone() {
    //fprintf(stderr, "fromBramDone\n");
    sem_post(&mul_sem);
  }
  virtual void statesDone() {
    //fprintf(stderr, "statesDone\n");
    sem_post(&mul_sem);
  }
  virtual void updateWeightsDone() {
    //fprintf(stderr, "updateWeightsDone\n");
    sem_post(&mul_sem);
  }
  virtual void sumOfErrorSquared(uint32_t x) {
    //fprintf(stderr, "sumOfErrorSquared error=%f\n", *(float *)&x);
    sem_post(&mul_sem);
  }
  virtual void dbg(uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    fprintf(stderr, "rbm dbg a=%x b=%x c=%x d=%x\n", a, b, c, d);
  }
};

class SigmoidIndication : public SigmoidIndicationWrapper
{
public:
 SigmoidIndication(int id) : SigmoidIndicationWrapper(id) {
  }
  virtual ~SigmoidIndication() {}
  virtual void sigmoidDone() {
    fprintf(stderr, "sigmoidDone\n");
    sem_post(&mul_sem);
  }
  virtual void tableUpdated(uint32_t addr) {
    sem_post(&mul_sem);
  }
  uint32_t tableSize() { return tableSize_; }
  virtual void tableSize(uint32_t size) {
    fprintf(stderr, "sigmoidTableSize %d\n", size);
    tableSize_ = size;
    sem_post(&mul_sem);
  }
 private:
  uint32_t tableSize_;
};

//  void sigmoid(PortalMat &a);


float sigmoid(float x);
void configureSigmoidTable(RbmRequestProxy *device, RbmIndication *indication);

class RBM {
 public:
  RBM(DmaManager *dma) : dma(dma) {}
  void train(int numVisible, int numHidden, const cv::Mat &trainingData);
  void run();
 private:
  DmaManager *dma;
};

class RbmMat : public PortalMat {
 public:
  RbmMat() : PortalMat() {};
  RbmMat(const RbmMat &m) : PortalMat(m) {};
  RbmMat(const cv::Mat &m) : PortalMat(m) {};
  void sigmoid(RbmMat &a);
  void hiddenStates(RbmMat &a, RbmMat &rand);
  // weights += learningRate * (pos_associations - neg_associations) / num_examples;
  void updateWeights(RbmMat &posAssociations, RbmMat &negAssociations, float learningRateOverNumExamples);
  void sumOfErrorSquared(RbmMat &pred);
};

#endif // _RBM_H_
