﻿mindspore.dataset.text.GloVe
=============================

.. py:class:: mindspore.dataset.text.GloVe

    用于将tokens映射到矢量的GloVe对象。

    .. py:method:: from_file(file_path, max_vectors=None)

        从文件构建CharNGram向量。

        参数：
            - **file_path** (str) - 包含向量的文件的路径。预训练向量集的格式必须是 `glove.6B.*.txt` 。
            - **max_vectors** (int，可选) - 用于限制加载的预训练向量的数量。
              大多数预训练的向量集是按词频降序排序的。因此，在如果内存不能存放整个向量集，或者由于其他原因不需要，
              可以传递 `max_vectors` 限制加载数量。默认值：None，无限制。

        返回：
            GloVe， 根据文件构建的GloVe向量。
